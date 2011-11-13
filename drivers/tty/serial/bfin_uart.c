/*
 * Blackfin On-Chip Serial Driver
 *
 * Copyright 2006-2010 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#if defined(CONFIG_SERIAL_BFIN_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#define DRIVER_NAME "bfin-uart"
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/kgdb.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <asm/portmux.h>
#include <asm/cacheflush.h>
#include <asm/dma.h>

#define port_membase(uart)     (((struct bfin_serial_port *)(uart))->port.membase)
#define get_lsr_cache(uart)    (((struct bfin_serial_port *)(uart))->lsr)
#define put_lsr_cache(uart, v) (((struct bfin_serial_port *)(uart))->lsr = (v))
#include <asm/bfin_serial.h>

#ifdef CONFIG_SERIAL_BFIN_MODULE
# undef CONFIG_EARLY_PRINTK
#endif

#ifdef CONFIG_SERIAL_BFIN_MODULE
# undef CONFIG_EARLY_PRINTK
#endif

/* UART name and device definitions */
#define BFIN_SERIAL_DEV_NAME	"ttyBF"
#define BFIN_SERIAL_MAJOR	204
#define BFIN_SERIAL_MINOR	64

static struct bfin_serial_port *bfin_serial_ports[BFIN_UART_NR_PORTS];

#if defined(CONFIG_KGDB_SERIAL_CONSOLE) || \
	defined(CONFIG_KGDB_SERIAL_CONSOLE_MODULE)

# ifndef CONFIG_SERIAL_BFIN_PIO
#  error KGDB only support UART in PIO mode.
# endif

static int kgdboc_port_line;
static int kgdboc_break_enabled;
#endif
/*
 * Setup for console. Argument comes from the menuconfig
 */
#define DMA_RX_XCOUNT		512
#define DMA_RX_YCOUNT		(PAGE_SIZE / DMA_RX_XCOUNT)

#define DMA_RX_FLUSH_JIFFIES	(HZ / 50)

#ifdef CONFIG_SERIAL_BFIN_DMA
static void bfin_serial_dma_tx_chars(struct bfin_serial_port *uart);
#else
static void bfin_serial_tx_chars(struct bfin_serial_port *uart);
#endif

static void bfin_serial_reset_irda(struct uart_port *port);

#if defined(CONFIG_SERIAL_BFIN_CTSRTS) || \
	defined(CONFIG_SERIAL_BFIN_HARD_CTSRTS)
static unsigned int bfin_serial_get_mctrl(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	if (uart->cts_pin < 0)
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;

	/* CTS PIN is negative assertive. */
	if (UART_GET_CTS(uart))
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
	else
		return TIOCM_DSR | TIOCM_CAR;
}

static void bfin_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	if (uart->rts_pin < 0)
		return;

	/* RTS PIN is negative assertive. */
	if (mctrl & TIOCM_RTS)
		UART_ENABLE_RTS(uart);
	else
		UART_DISABLE_RTS(uart);
}

/*
 * Handle any change of modem status signal.
 */
static irqreturn_t bfin_serial_mctrl_cts_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;
	unsigned int status;

	status = bfin_serial_get_mctrl(&uart->port);
	uart_handle_cts_change(&uart->port, status & TIOCM_CTS);
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	uart->scts = 1;
	UART_CLEAR_SCTS(uart);
	UART_CLEAR_IER(uart, EDSSI);
#endif

	return IRQ_HANDLED;
}
#else
static unsigned int bfin_serial_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void bfin_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}
#endif

/*
 * interrupts are disabled on entry
 */
static void bfin_serial_stop_tx(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
#ifdef CONFIG_SERIAL_BFIN_DMA
	struct circ_buf *xmit = &uart->port.state->xmit;
#endif

	while (!(UART_GET_LSR(uart) & TEMT))
		cpu_relax();

#ifdef CONFIG_SERIAL_BFIN_DMA
	disable_dma(uart->tx_dma_channel);
	xmit->tail = (xmit->tail + uart->tx_count) & (UART_XMIT_SIZE - 1);
	uart->port.icount.tx += uart->tx_count;
	uart->tx_count = 0;
	uart->tx_done = 1;
#else
#ifdef CONFIG_BF54x
	/* Clear TFI bit */
	UART_PUT_LSR(uart, TFI);
#endif
	UART_CLEAR_IER(uart, ETBEI);
#endif
}

/*
 * port is locked and interrupts are disabled
 */
static void bfin_serial_start_tx(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	struct tty_struct *tty = uart->port.state->port.tty;

#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	if (uart->scts && !(bfin_serial_get_mctrl(&uart->port) & TIOCM_CTS)) {
		uart->scts = 0;
		uart_handle_cts_change(&uart->port, uart->scts);
	}
#endif

	/*
	 * To avoid losting RX interrupt, we reset IR function
	 * before sending data.
	 */
	if (tty->termios->c_line == N_IRDA)
		bfin_serial_reset_irda(port);

#ifdef CONFIG_SERIAL_BFIN_DMA
	if (uart->tx_done)
		bfin_serial_dma_tx_chars(uart);
#else
	UART_SET_IER(uart, ETBEI);
	bfin_serial_tx_chars(uart);
#endif
}

/*
 * Interrupts are enabled
 */
static void bfin_serial_stop_rx(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

	UART_CLEAR_IER(uart, ERBFI);
}

/*
 * Set the modem control timer to fire immediately.
 */
static void bfin_serial_enable_ms(struct uart_port *port)
{
}


#if ANOMALY_05000363 && defined(CONFIG_SERIAL_BFIN_PIO)
# define UART_GET_ANOMALY_THRESHOLD(uart)    ((uart)->anomaly_threshold)
# define UART_SET_ANOMALY_THRESHOLD(uart, v) ((uart)->anomaly_threshold = (v))
#else
# define UART_GET_ANOMALY_THRESHOLD(uart)    0
# define UART_SET_ANOMALY_THRESHOLD(uart, v)
#endif

#ifdef CONFIG_SERIAL_BFIN_PIO
static void bfin_serial_rx_chars(struct bfin_serial_port *uart)
{
	struct tty_struct *tty = NULL;
	unsigned int status, ch, flg;
	static struct timeval anomaly_start = { .tv_sec = 0 };

	status = UART_GET_LSR(uart);
	UART_CLEAR_LSR(uart);

	ch = UART_GET_CHAR(uart);
	uart->port.icount.rx++;

#if defined(CONFIG_KGDB_SERIAL_CONSOLE) || \
	defined(CONFIG_KGDB_SERIAL_CONSOLE_MODULE)
	if (kgdb_connected && kgdboc_port_line == uart->port.line
		&& kgdboc_break_enabled)
		if (ch == 0x3) {/* Ctrl + C */
			kgdb_breakpoint();
			return;
		}

	if (!uart->port.state || !uart->port.state->port.tty)
		return;
#endif
	tty = uart->port.state->port.tty;

	if (ANOMALY_05000363) {
		/* The BF533 (and BF561) family of processors have a nice anomaly
		 * where they continuously generate characters for a "single" break.
		 * We have to basically ignore this flood until the "next" valid
		 * character comes across.  Due to the nature of the flood, it is
		 * not possible to reliably catch bytes that are sent too quickly
		 * after this break.  So application code talking to the Blackfin
		 * which sends a break signal must allow at least 1.5 character
		 * times after the end of the break for things to stabilize.  This
		 * timeout was picked as it must absolutely be larger than 1
		 * character time +/- some percent.  So 1.5 sounds good.  All other
		 * Blackfin families operate properly.  Woo.
		 */
		if (anomaly_start.tv_sec) {
			struct timeval curr;
			suseconds_t usecs;

			if ((~ch & (~ch + 1)) & 0xff)
				goto known_good_char;

			do_gettimeofday(&curr);
			if (curr.tv_sec - anomaly_start.tv_sec > 1)
				goto known_good_char;

			usecs = 0;
			if (curr.tv_sec != anomaly_start.tv_sec)
				usecs += USEC_PER_SEC;
			usecs += curr.tv_usec - anomaly_start.tv_usec;

			if (usecs > UART_GET_ANOMALY_THRESHOLD(uart))
				goto known_good_char;

			if (ch)
				anomaly_start.tv_sec = 0;
			else
				anomaly_start = curr;

			return;

 known_good_char:
			status &= ~BI;
			anomaly_start.tv_sec = 0;
		}
	}

	if (status & BI) {
		if (ANOMALY_05000363)
			if (bfin_revid() < 5)
				do_gettimeofday(&anomaly_start);
		uart->port.icount.brk++;
		if (uart_handle_break(&uart->port))
			goto ignore_char;
		status &= ~(PE | FE);
	}
	if (status & PE)
		uart->port.icount.parity++;
	if (status & OE)
		uart->port.icount.overrun++;
	if (status & FE)
		uart->port.icount.frame++;

	status &= uart->port.read_status_mask;

	if (status & BI)
		flg = TTY_BREAK;
	else if (status & PE)
		flg = TTY_PARITY;
	else if (status & FE)
		flg = TTY_FRAME;
	else
		flg = TTY_NORMAL;

	if (uart_handle_sysrq_char(&uart->port, ch))
		goto ignore_char;

	uart_insert_char(&uart->port, status, OE, ch, flg);

 ignore_char:
	tty_flip_buffer_push(tty);
}

static void bfin_serial_tx_chars(struct bfin_serial_port *uart)
{
	struct circ_buf *xmit = &uart->port.state->xmit;

	if (uart_circ_empty(xmit) || uart_tx_stopped(&uart->port)) {
#ifdef CONFIG_BF54x
		/* Clear TFI bit */
		UART_PUT_LSR(uart, TFI);
#endif
		/* Anomaly notes:
		 *  05000215 -	we always clear ETBEI within last UART TX
		 *		interrupt to end a string. It is always set
		 *		when start a new tx.
		 */
		UART_CLEAR_IER(uart, ETBEI);
		return;
	}

	if (uart->port.x_char) {
		UART_PUT_CHAR(uart, uart->port.x_char);
		uart->port.icount.tx++;
		uart->port.x_char = 0;
	}

	while ((UART_GET_LSR(uart) & THRE) && xmit->tail != xmit->head) {
		UART_PUT_CHAR(uart, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		uart->port.icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&uart->port);
}

static irqreturn_t bfin_serial_rx_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;

	while (UART_GET_LSR(uart) & DR)
		bfin_serial_rx_chars(uart);

	return IRQ_HANDLED;
}

static irqreturn_t bfin_serial_tx_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;

#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	if (uart->scts && !(bfin_serial_get_mctrl(&uart->port) & TIOCM_CTS)) {
		uart->scts = 0;
		uart_handle_cts_change(&uart->port, uart->scts);
	}
#endif
	spin_lock(&uart->port.lock);
	if (UART_GET_LSR(uart) & THRE)
		bfin_serial_tx_chars(uart);
	spin_unlock(&uart->port.lock);

	return IRQ_HANDLED;
}
#endif

#ifdef CONFIG_SERIAL_BFIN_DMA
static void bfin_serial_dma_tx_chars(struct bfin_serial_port *uart)
{
	struct circ_buf *xmit = &uart->port.state->xmit;

	uart->tx_done = 0;

	if (uart_circ_empty(xmit) || uart_tx_stopped(&uart->port)) {
		uart->tx_count = 0;
		uart->tx_done = 1;
		return;
	}

	if (uart->port.x_char) {
		UART_PUT_CHAR(uart, uart->port.x_char);
		uart->port.icount.tx++;
		uart->port.x_char = 0;
	}

	uart->tx_count = CIRC_CNT(xmit->head, xmit->tail, UART_XMIT_SIZE);
	if (uart->tx_count > (UART_XMIT_SIZE - xmit->tail))
		uart->tx_count = UART_XMIT_SIZE - xmit->tail;
	blackfin_dcache_flush_range((unsigned long)(xmit->buf+xmit->tail),
					(unsigned long)(xmit->buf+xmit->tail+uart->tx_count));
	set_dma_config(uart->tx_dma_channel,
		set_bfin_dma_config(DIR_READ, DMA_FLOW_STOP,
			INTR_ON_BUF,
			DIMENSION_LINEAR,
			DATA_SIZE_8,
			DMA_SYNC_RESTART));
	set_dma_start_addr(uart->tx_dma_channel, (unsigned long)(xmit->buf+xmit->tail));
	set_dma_x_count(uart->tx_dma_channel, uart->tx_count);
	set_dma_x_modify(uart->tx_dma_channel, 1);
	SSYNC();
	enable_dma(uart->tx_dma_channel);

	UART_SET_IER(uart, ETBEI);
}

static void bfin_serial_dma_rx_chars(struct bfin_serial_port *uart)
{
	struct tty_struct *tty = uart->port.state->port.tty;
	int i, flg, status;

	status = UART_GET_LSR(uart);
	UART_CLEAR_LSR(uart);

	uart->port.icount.rx +=
		CIRC_CNT(uart->rx_dma_buf.head, uart->rx_dma_buf.tail,
		UART_XMIT_SIZE);

	if (status & BI) {
		uart->port.icount.brk++;
		if (uart_handle_break(&uart->port))
			goto dma_ignore_char;
		status &= ~(PE | FE);
	}
	if (status & PE)
		uart->port.icount.parity++;
	if (status & OE)
		uart->port.icount.overrun++;
	if (status & FE)
		uart->port.icount.frame++;

	status &= uart->port.read_status_mask;

	if (status & BI)
		flg = TTY_BREAK;
	else if (status & PE)
		flg = TTY_PARITY;
	else if (status & FE)
		flg = TTY_FRAME;
	else
		flg = TTY_NORMAL;

	for (i = uart->rx_dma_buf.tail; ; i++) {
		if (i >= UART_XMIT_SIZE)
			i = 0;
		if (i == uart->rx_dma_buf.head)
			break;
		if (!uart_handle_sysrq_char(&uart->port, uart->rx_dma_buf.buf[i]))
			uart_insert_char(&uart->port, status, OE,
				uart->rx_dma_buf.buf[i], flg);
	}

 dma_ignore_char:
	tty_flip_buffer_push(tty);
}

void bfin_serial_rx_dma_timeout(struct bfin_serial_port *uart)
{
	int x_pos, pos;

	dma_disable_irq_nosync(uart->rx_dma_channel);
	spin_lock_bh(&uart->rx_lock);

	/* 2D DMA RX buffer ring is used. Because curr_y_count and
	 * curr_x_count can't be read as an atomic operation,
	 * curr_y_count should be read before curr_x_count. When
	 * curr_x_count is read, curr_y_count may already indicate
	 * next buffer line. But, the position calculated here is
	 * still indicate the old line. The wrong position data may
	 * be smaller than current buffer tail, which cause garbages
	 * are received if it is not prohibit.
	 */
	uart->rx_dma_nrows = get_dma_curr_ycount(uart->rx_dma_channel);
	x_pos = get_dma_curr_xcount(uart->rx_dma_channel);
	uart->rx_dma_nrows = DMA_RX_YCOUNT - uart->rx_dma_nrows;
	if (uart->rx_dma_nrows == DMA_RX_YCOUNT || x_pos == 0)
		uart->rx_dma_nrows = 0;
	x_pos = DMA_RX_XCOUNT - x_pos;
	if (x_pos == DMA_RX_XCOUNT)
		x_pos = 0;

	pos = uart->rx_dma_nrows * DMA_RX_XCOUNT + x_pos;
	/* Ignore receiving data if new position is in the same line of
	 * current buffer tail and small.
	 */
	if (pos > uart->rx_dma_buf.tail ||
		uart->rx_dma_nrows < (uart->rx_dma_buf.tail/DMA_RX_XCOUNT)) {
		uart->rx_dma_buf.head = pos;
		bfin_serial_dma_rx_chars(uart);
		uart->rx_dma_buf.tail = uart->rx_dma_buf.head;
	}

	spin_unlock_bh(&uart->rx_lock);
	dma_enable_irq(uart->rx_dma_channel);

	mod_timer(&(uart->rx_dma_timer), jiffies + DMA_RX_FLUSH_JIFFIES);
}

static irqreturn_t bfin_serial_dma_tx_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;
	struct circ_buf *xmit = &uart->port.state->xmit;

#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	if (uart->scts && !(bfin_serial_get_mctrl(&uart->port)&TIOCM_CTS)) {
		uart->scts = 0;
		uart_handle_cts_change(&uart->port, uart->scts);
	}
#endif

	spin_lock(&uart->port.lock);
	if (!(get_dma_curr_irqstat(uart->tx_dma_channel)&DMA_RUN)) {
		disable_dma(uart->tx_dma_channel);
		clear_dma_irqstat(uart->tx_dma_channel);
		/* Anomaly notes:
		 *  05000215 -	we always clear ETBEI within last UART TX
		 *		interrupt to end a string. It is always set
		 *		when start a new tx.
		 */
		UART_CLEAR_IER(uart, ETBEI);
		xmit->tail = (xmit->tail + uart->tx_count) & (UART_XMIT_SIZE - 1);
		uart->port.icount.tx += uart->tx_count;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_write_wakeup(&uart->port);

		bfin_serial_dma_tx_chars(uart);
	}

	spin_unlock(&uart->port.lock);
	return IRQ_HANDLED;
}

static irqreturn_t bfin_serial_dma_rx_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;
	unsigned short irqstat;
	int x_pos, pos;

	spin_lock(&uart->rx_lock);
	irqstat = get_dma_curr_irqstat(uart->rx_dma_channel);
	clear_dma_irqstat(uart->rx_dma_channel);

	uart->rx_dma_nrows = get_dma_curr_ycount(uart->rx_dma_channel);
	x_pos = get_dma_curr_xcount(uart->rx_dma_channel);
	uart->rx_dma_nrows = DMA_RX_YCOUNT - uart->rx_dma_nrows;
	if (uart->rx_dma_nrows == DMA_RX_YCOUNT || x_pos == 0)
		uart->rx_dma_nrows = 0;

	pos = uart->rx_dma_nrows * DMA_RX_XCOUNT;
	if (pos > uart->rx_dma_buf.tail ||
		uart->rx_dma_nrows < (uart->rx_dma_buf.tail/DMA_RX_XCOUNT)) {
		uart->rx_dma_buf.head = pos;
		bfin_serial_dma_rx_chars(uart);
		uart->rx_dma_buf.tail = uart->rx_dma_buf.head;
	}

	spin_unlock(&uart->rx_lock);

	return IRQ_HANDLED;
}
#endif

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int bfin_serial_tx_empty(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned short lsr;

	lsr = UART_GET_LSR(uart);
	if (lsr & TEMT)
		return TIOCSER_TEMT;
	else
		return 0;
}

static void bfin_serial_break_ctl(struct uart_port *port, int break_state)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	u16 lcr = UART_GET_LCR(uart);
	if (break_state)
		lcr |= SB;
	else
		lcr &= ~SB;
	UART_PUT_LCR(uart, lcr);
	SSYNC();
}

static int bfin_serial_startup(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

#ifdef CONFIG_SERIAL_BFIN_DMA
	dma_addr_t dma_handle;

	if (request_dma(uart->rx_dma_channel, "BFIN_UART_RX") < 0) {
		printk(KERN_NOTICE "Unable to attach Blackfin UART RX DMA channel\n");
		return -EBUSY;
	}

	if (request_dma(uart->tx_dma_channel, "BFIN_UART_TX") < 0) {
		printk(KERN_NOTICE "Unable to attach Blackfin UART TX DMA channel\n");
		free_dma(uart->rx_dma_channel);
		return -EBUSY;
	}

	set_dma_callback(uart->rx_dma_channel, bfin_serial_dma_rx_int, uart);
	set_dma_callback(uart->tx_dma_channel, bfin_serial_dma_tx_int, uart);

	uart->rx_dma_buf.buf = (unsigned char *)dma_alloc_coherent(NULL, PAGE_SIZE, &dma_handle, GFP_DMA);
	uart->rx_dma_buf.head = 0;
	uart->rx_dma_buf.tail = 0;
	uart->rx_dma_nrows = 0;

	set_dma_config(uart->rx_dma_channel,
		set_bfin_dma_config(DIR_WRITE, DMA_FLOW_AUTO,
				INTR_ON_ROW, DIMENSION_2D,
				DATA_SIZE_8,
				DMA_SYNC_RESTART));
	set_dma_x_count(uart->rx_dma_channel, DMA_RX_XCOUNT);
	set_dma_x_modify(uart->rx_dma_channel, 1);
	set_dma_y_count(uart->rx_dma_channel, DMA_RX_YCOUNT);
	set_dma_y_modify(uart->rx_dma_channel, 1);
	set_dma_start_addr(uart->rx_dma_channel, (unsigned long)uart->rx_dma_buf.buf);
	enable_dma(uart->rx_dma_channel);

	uart->rx_dma_timer.data = (unsigned long)(uart);
	uart->rx_dma_timer.function = (void *)bfin_serial_rx_dma_timeout;
	uart->rx_dma_timer.expires = jiffies + DMA_RX_FLUSH_JIFFIES;
	add_timer(&(uart->rx_dma_timer));
#else
# if defined(CONFIG_KGDB_SERIAL_CONSOLE) || \
	defined(CONFIG_KGDB_SERIAL_CONSOLE_MODULE)
	if (kgdboc_port_line == uart->port.line && kgdboc_break_enabled)
		kgdboc_break_enabled = 0;
	else {
# endif
	if (request_irq(uart->rx_irq, bfin_serial_rx_int, 0,
	     "BFIN_UART_RX", uart)) {
		printk(KERN_NOTICE "Unable to attach BlackFin UART RX interrupt\n");
		return -EBUSY;
	}

	if (request_irq
	    (uart->tx_irq, bfin_serial_tx_int, 0,
	     "BFIN_UART_TX", uart)) {
		printk(KERN_NOTICE "Unable to attach BlackFin UART TX interrupt\n");
		free_irq(uart->rx_irq, uart);
		return -EBUSY;
	}

# ifdef CONFIG_BF54x
	{
		/*
		 * UART2 and UART3 on BF548 share interrupt PINs and DMA
		 * controllers with SPORT2 and SPORT3. UART rx and tx
		 * interrupts are generated in PIO mode only when configure
		 * their peripheral mapping registers properly, which means
		 * request corresponding DMA channels in PIO mode as well.
		 */
		unsigned uart_dma_ch_rx, uart_dma_ch_tx;

		switch (uart->rx_irq) {
		case IRQ_UART3_RX:
			uart_dma_ch_rx = CH_UART3_RX;
			uart_dma_ch_tx = CH_UART3_TX;
			break;
		case IRQ_UART2_RX:
			uart_dma_ch_rx = CH_UART2_RX;
			uart_dma_ch_tx = CH_UART2_TX;
			break;
		default:
			uart_dma_ch_rx = uart_dma_ch_tx = 0;
			break;
		};

		if (uart_dma_ch_rx &&
			request_dma(uart_dma_ch_rx, "BFIN_UART_RX") < 0) {
			printk(KERN_NOTICE"Fail to attach UART interrupt\n");
			free_irq(uart->rx_irq, uart);
			free_irq(uart->tx_irq, uart);
			return -EBUSY;
		}
		if (uart_dma_ch_tx &&
			request_dma(uart_dma_ch_tx, "BFIN_UART_TX") < 0) {
			printk(KERN_NOTICE "Fail to attach UART interrupt\n");
			free_dma(uart_dma_ch_rx);
			free_irq(uart->rx_irq, uart);
			free_irq(uart->tx_irq, uart);
			return -EBUSY;
		}
	}
# endif
# if defined(CONFIG_KGDB_SERIAL_CONSOLE) || \
	defined(CONFIG_KGDB_SERIAL_CONSOLE_MODULE)
	}
# endif
#endif

#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	if (uart->cts_pin >= 0) {
		if (request_irq(gpio_to_irq(uart->cts_pin),
			bfin_serial_mctrl_cts_int,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			0, "BFIN_UART_CTS", uart)) {
			uart->cts_pin = -1;
			pr_info("Unable to attach BlackFin UART CTS interrupt. So, disable it.\n");
		}
	}
	if (uart->rts_pin >= 0)
		gpio_direction_output(uart->rts_pin, 0);
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	if (uart->cts_pin >= 0 && request_irq(uart->status_irq,
		bfin_serial_mctrl_cts_int,
		0, "BFIN_UART_MODEM_STATUS", uart)) {
		uart->cts_pin = -1;
		pr_info("Unable to attach BlackFin UART Modem Status interrupt.\n");
	}

	/* CTS RTS PINs are negative assertive. */
	UART_PUT_MCR(uart, ACTS);
	UART_SET_IER(uart, EDSSI);
#endif

	UART_SET_IER(uart, ERBFI);
	return 0;
}

static void bfin_serial_shutdown(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

#ifdef CONFIG_SERIAL_BFIN_DMA
	disable_dma(uart->tx_dma_channel);
	free_dma(uart->tx_dma_channel);
	disable_dma(uart->rx_dma_channel);
	free_dma(uart->rx_dma_channel);
	del_timer(&(uart->rx_dma_timer));
	dma_free_coherent(NULL, PAGE_SIZE, uart->rx_dma_buf.buf, 0);
#else
#ifdef CONFIG_BF54x
	switch (uart->port.irq) {
	case IRQ_UART3_RX:
		free_dma(CH_UART3_RX);
		free_dma(CH_UART3_TX);
		break;
	case IRQ_UART2_RX:
		free_dma(CH_UART2_RX);
		free_dma(CH_UART2_TX);
		break;
	default:
		break;
	};
#endif
	free_irq(uart->rx_irq, uart);
	free_irq(uart->tx_irq, uart);
#endif

#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	if (uart->cts_pin >= 0)
		free_irq(gpio_to_irq(uart->cts_pin), uart);
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	if (uart->cts_pin >= 0)
		free_irq(uart->status_irq, uart);
#endif
}

static void
bfin_serial_set_termios(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned long flags;
	unsigned int baud, quot;
	unsigned short val, ier, lcr = 0;

	switch (termios->c_cflag & CSIZE) {
	case CS8:
		lcr = WLS(8);
		break;
	case CS7:
		lcr = WLS(7);
		break;
	case CS6:
		lcr = WLS(6);
		break;
	case CS5:
		lcr = WLS(5);
		break;
	default:
		printk(KERN_ERR "%s: word lengh not supported\n",
			__func__);
	}

	/* Anomaly notes:
	 *  05000231 -  STOP bit is always set to 1 whatever the user is set.
	 */
	if (termios->c_cflag & CSTOPB) {
		if (ANOMALY_05000231)
			printk(KERN_WARNING "STOP bits other than 1 is not "
				"supported in case of anomaly 05000231.\n");
		else
			lcr |= STB;
	}
	if (termios->c_cflag & PARENB)
		lcr |= PEN;
	if (!(termios->c_cflag & PARODD))
		lcr |= EPS;
	if (termios->c_cflag & CMSPAR)
		lcr |= STP;

	spin_lock_irqsave(&uart->port.lock, flags);

	port->read_status_mask = OE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= (FE | PE);
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= BI;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= FE | PE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= OE;
	}

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);

	/* If discipline is not IRDA, apply ANOMALY_05000230 */
	if (termios->c_line != N_IRDA)
		quot -= ANOMALY_05000230;

	UART_SET_ANOMALY_THRESHOLD(uart, USEC_PER_SEC / baud * 15);

	/* Disable UART */
	ier = UART_GET_IER(uart);
	UART_DISABLE_INTS(uart);

	/* Set DLAB in LCR to Access DLL and DLH */
	UART_SET_DLAB(uart);

	UART_PUT_DLL(uart, quot & 0xFF);
	UART_PUT_DLH(uart, (quot >> 8) & 0xFF);
	SSYNC();

	/* Clear DLAB in LCR to Access THR RBR IER */
	UART_CLEAR_DLAB(uart);

	UART_PUT_LCR(uart, lcr);

	/* Enable UART */
	UART_ENABLE_INTS(uart, ier);

	val = UART_GET_GCTL(uart);
	val |= UCEN;
	UART_PUT_GCTL(uart, val);

	/* Port speed changed, update the per-port timeout. */
	uart_update_timeout(port, termios->c_cflag, baud);

	spin_unlock_irqrestore(&uart->port.lock, flags);
}

static const char *bfin_serial_type(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

	return uart->port.type == PORT_BFIN ? "BFIN-UART" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void bfin_serial_release_port(struct uart_port *port)
{
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int bfin_serial_request_port(struct uart_port *port)
{
	return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void bfin_serial_config_port(struct uart_port *port, int flags)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

	if (flags & UART_CONFIG_TYPE &&
	    bfin_serial_request_port(&uart->port) == 0)
		uart->port.type = PORT_BFIN;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 * The only change we allow are to the flags and type, and
 * even then only between PORT_BFIN and PORT_UNKNOWN
 */
static int
bfin_serial_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return 0;
}

/*
 * Enable the IrDA function if tty->ldisc.num is N_IRDA.
 * In other cases, disable IrDA function.
 */
static void bfin_serial_set_ldisc(struct uart_port *port, int ld)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned short val;

	switch (ld) {
	case N_IRDA:
		val = UART_GET_GCTL(uart);
		val |= (IREN | RPOLC);
		UART_PUT_GCTL(uart, val);
		break;
	default:
		val = UART_GET_GCTL(uart);
		val &= ~(IREN | RPOLC);
		UART_PUT_GCTL(uart, val);
	}
}

static void bfin_serial_reset_irda(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned short val;

	val = UART_GET_GCTL(uart);
	val &= ~(IREN | RPOLC);
	UART_PUT_GCTL(uart, val);
	SSYNC();
	val |= (IREN | RPOLC);
	UART_PUT_GCTL(uart, val);
	SSYNC();
}

#ifdef CONFIG_CONSOLE_POLL
/* Anomaly notes:
 *  05000099 -  Because we only use THRE in poll_put and DR in poll_get,
 *		losing other bits of UART_LSR is not a problem here.
 */
static void bfin_serial_poll_put_char(struct uart_port *port, unsigned char chr)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

	while (!(UART_GET_LSR(uart) & THRE))
		cpu_relax();

	UART_CLEAR_DLAB(uart);
	UART_PUT_CHAR(uart, (unsigned char)chr);
}

static int bfin_serial_poll_get_char(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned char chr;

	while (!(UART_GET_LSR(uart) & DR))
		cpu_relax();

	UART_CLEAR_DLAB(uart);
	chr = UART_GET_CHAR(uart);

	return chr;
}
#endif

#if defined(CONFIG_KGDB_SERIAL_CONSOLE) || \
	defined(CONFIG_KGDB_SERIAL_CONSOLE_MODULE)
static void bfin_kgdboc_port_shutdown(struct uart_port *port)
{
	if (kgdboc_break_enabled) {
		kgdboc_break_enabled = 0;
		bfin_serial_shutdown(port);
	}
}

static int bfin_kgdboc_port_startup(struct uart_port *port)
{
	kgdboc_port_line = port->line;
	kgdboc_break_enabled = !bfin_serial_startup(port);
	return 0;
}
#endif

static struct uart_ops bfin_serial_pops = {
	.tx_empty	= bfin_serial_tx_empty,
	.set_mctrl	= bfin_serial_set_mctrl,
	.get_mctrl	= bfin_serial_get_mctrl,
	.stop_tx	= bfin_serial_stop_tx,
	.start_tx	= bfin_serial_start_tx,
	.stop_rx	= bfin_serial_stop_rx,
	.enable_ms	= bfin_serial_enable_ms,
	.break_ctl	= bfin_serial_break_ctl,
	.startup	= bfin_serial_startup,
	.shutdown	= bfin_serial_shutdown,
	.set_termios	= bfin_serial_set_termios,
	.set_ldisc	= bfin_serial_set_ldisc,
	.type		= bfin_serial_type,
	.release_port	= bfin_serial_release_port,
	.request_port	= bfin_serial_request_port,
	.config_port	= bfin_serial_config_port,
	.verify_port	= bfin_serial_verify_port,
#if defined(CONFIG_KGDB_SERIAL_CONSOLE) || \
	defined(CONFIG_KGDB_SERIAL_CONSOLE_MODULE)
	.kgdboc_port_startup	= bfin_kgdboc_port_startup,
	.kgdboc_port_shutdown	= bfin_kgdboc_port_shutdown,
#endif
#ifdef CONFIG_CONSOLE_POLL
	.poll_put_char	= bfin_serial_poll_put_char,
	.poll_get_char	= bfin_serial_poll_get_char,
#endif
};

#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
/*
 * If the port was already initialised (eg, by a boot loader),
 * try to determine the current setup.
 */
static void __init
bfin_serial_console_get_options(struct bfin_serial_port *uart, int *baud,
			   int *parity, int *bits)
{
	unsigned short status;

	status = UART_GET_IER(uart) & (ERBFI | ETBEI);
	if (status == (ERBFI | ETBEI)) {
		/* ok, the port was enabled */
		u16 lcr, dlh, dll;

		lcr = UART_GET_LCR(uart);

		*parity = 'n';
		if (lcr & PEN) {
			if (lcr & EPS)
				*parity = 'e';
			else
				*parity = 'o';
		}
		switch (lcr & 0x03) {
		case 0:
			*bits = 5;
			break;
		case 1:
			*bits = 6;
			break;
		case 2:
			*bits = 7;
			break;
		case 3:
			*bits = 8;
			break;
		}
		/* Set DLAB in LCR to Access DLL and DLH */
		UART_SET_DLAB(uart);

		dll = UART_GET_DLL(uart);
		dlh = UART_GET_DLH(uart);

		/* Clear DLAB in LCR to Access THR RBR IER */
		UART_CLEAR_DLAB(uart);

		*baud = get_sclk() / (16*(dll | dlh << 8));
	}
	pr_debug("%s:baud = %d, parity = %c, bits= %d\n", __func__, *baud, *parity, *bits);
}

static struct uart_driver bfin_serial_reg;

static void bfin_serial_console_putchar(struct uart_port *port, int ch)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	while (!(UART_GET_LSR(uart) & THRE))
		barrier();
	UART_PUT_CHAR(uart, ch);
}

#endif /* defined (CONFIG_SERIAL_BFIN_CONSOLE) ||
		 defined (CONFIG_EARLY_PRINTK) */

#ifdef CONFIG_SERIAL_BFIN_CONSOLE
#define CLASS_BFIN_CONSOLE	"bfin-console"
/*
 * Interrupts are disabled on entering
 */
static void
bfin_serial_console_write(struct console *co, const char *s, unsigned int count)
{
	struct bfin_serial_port *uart = bfin_serial_ports[co->index];
	unsigned long flags;

	spin_lock_irqsave(&uart->port.lock, flags);
	uart_console_write(&uart->port, s, count, bfin_serial_console_putchar);
	spin_unlock_irqrestore(&uart->port.lock, flags);

}

static int __init
bfin_serial_console_setup(struct console *co, char *options)
{
	struct bfin_serial_port *uart;
	int baud = 57600;
	int bits = 8;
	int parity = 'n';
# if defined(CONFIG_SERIAL_BFIN_CTSRTS) || \
	defined(CONFIG_SERIAL_BFIN_HARD_CTSRTS)
	int flow = 'r';
# else
	int flow = 'n';
# endif

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index < 0 || co->index >= BFIN_UART_NR_PORTS)
		return -ENODEV;

	uart = bfin_serial_ports[co->index];
	if (!uart)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		bfin_serial_console_get_options(uart, &baud, &parity, &bits);

	return uart_set_options(&uart->port, co, baud, parity, bits, flow);
}

static struct console bfin_serial_console = {
	.name		= BFIN_SERIAL_DEV_NAME,
	.write		= bfin_serial_console_write,
	.device		= uart_console_device,
	.setup		= bfin_serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &bfin_serial_reg,
};
#define BFIN_SERIAL_CONSOLE	(&bfin_serial_console)
#else
#define BFIN_SERIAL_CONSOLE	NULL
#endif /* CONFIG_SERIAL_BFIN_CONSOLE */

#ifdef	CONFIG_EARLY_PRINTK
static struct bfin_serial_port bfin_earlyprintk_port;
#define CLASS_BFIN_EARLYPRINTK	"bfin-earlyprintk"

/*
 * Interrupts are disabled on entering
 */
static void
bfin_earlyprintk_console_write(struct console *co, const char *s, unsigned int count)
{
	unsigned long flags;

	if (bfin_earlyprintk_port.port.line != co->index)
		return;

	spin_lock_irqsave(&bfin_earlyprintk_port.port.lock, flags);
	uart_console_write(&bfin_earlyprintk_port.port, s, count,
		bfin_serial_console_putchar);
	spin_unlock_irqrestore(&bfin_earlyprintk_port.port.lock, flags);
}

/*
 * This should have a .setup or .early_setup in it, but then things get called
 * without the command line options, and the baud rate gets messed up - so
 * don't let the common infrastructure play with things. (see calls to setup
 * & earlysetup in ./kernel/printk.c:register_console()
 */
static struct __initdata console bfin_early_serial_console = {
	.name = "early_BFuart",
	.write = bfin_earlyprintk_console_write,
	.device = uart_console_device,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data  = &bfin_serial_reg,
};
#endif

static struct uart_driver bfin_serial_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= DRIVER_NAME,
	.dev_name		= BFIN_SERIAL_DEV_NAME,
	.major			= BFIN_SERIAL_MAJOR,
	.minor			= BFIN_SERIAL_MINOR,
	.nr			= BFIN_UART_NR_PORTS,
	.cons			= BFIN_SERIAL_CONSOLE,
};

static int bfin_serial_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct bfin_serial_port *uart = platform_get_drvdata(pdev);

	return uart_suspend_port(&bfin_serial_reg, &uart->port);
}

static int bfin_serial_resume(struct platform_device *pdev)
{
	struct bfin_serial_port *uart = platform_get_drvdata(pdev);

	return uart_resume_port(&bfin_serial_reg, &uart->port);
}

static int bfin_serial_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct bfin_serial_port *uart = NULL;
	int ret = 0;

	if (pdev->id < 0 || pdev->id >= BFIN_UART_NR_PORTS) {
		dev_err(&pdev->dev, "Wrong bfin uart platform device id.\n");
		return -ENOENT;
	}

	if (bfin_serial_ports[pdev->id] == NULL) {

		uart = kzalloc(sizeof(*uart), GFP_KERNEL);
		if (!uart) {
			dev_err(&pdev->dev,
				"fail to malloc bfin_serial_port\n");
			return -ENOMEM;
		}
		bfin_serial_ports[pdev->id] = uart;

#ifdef CONFIG_EARLY_PRINTK
		if (!(bfin_earlyprintk_port.port.membase
			&& bfin_earlyprintk_port.port.line == pdev->id)) {
			/*
			 * If the peripheral PINs of current port is allocated
			 * in earlyprintk probe stage, don't do it again.
			 */
#endif
		ret = peripheral_request_list(
			(unsigned short *)pdev->dev.platform_data, DRIVER_NAME);
		if (ret) {
			dev_err(&pdev->dev,
				"fail to request bfin serial peripherals\n");
			goto out_error_free_mem;
		}
#ifdef CONFIG_EARLY_PRINTK
		}
#endif

		spin_lock_init(&uart->port.lock);
		uart->port.uartclk   = get_sclk();
		uart->port.fifosize  = BFIN_UART_TX_FIFO_SIZE;
		uart->port.ops       = &bfin_serial_pops;
		uart->port.line      = pdev->id;
		uart->port.iotype    = UPIO_MEM;
		uart->port.flags     = UPF_BOOT_AUTOCONF;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (res == NULL) {
			dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
			ret = -ENOENT;
			goto out_error_free_peripherals;
		}

		uart->port.membase = ioremap(res->start, resource_size(res));
		if (!uart->port.membase) {
			dev_err(&pdev->dev, "Cannot map uart IO\n");
			ret = -ENXIO;
			goto out_error_free_peripherals;
		}
		uart->port.mapbase = res->start;

		uart->tx_irq = platform_get_irq(pdev, 0);
		if (uart->tx_irq < 0) {
			dev_err(&pdev->dev, "No uart TX IRQ specified\n");
			ret = -ENOENT;
			goto out_error_unmap;
		}

		uart->rx_irq = platform_get_irq(pdev, 1);
		if (uart->rx_irq < 0) {
			dev_err(&pdev->dev, "No uart RX IRQ specified\n");
			ret = -ENOENT;
			goto out_error_unmap;
		}
		uart->port.irq = uart->rx_irq;

		uart->status_irq = platform_get_irq(pdev, 2);
		if (uart->status_irq < 0) {
			dev_err(&pdev->dev, "No uart status IRQ specified\n");
			ret = -ENOENT;
			goto out_error_unmap;
		}

#ifdef CONFIG_SERIAL_BFIN_DMA
		spin_lock_init(&uart->rx_lock);
		uart->tx_done	    = 1;
		uart->tx_count	    = 0;

		res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
		if (res == NULL) {
			dev_err(&pdev->dev, "No uart TX DMA channel specified\n");
			ret = -ENOENT;
			goto out_error_unmap;
		}
		uart->tx_dma_channel = res->start;

		res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
		if (res == NULL) {
			dev_err(&pdev->dev, "No uart RX DMA channel specified\n");
			ret = -ENOENT;
			goto out_error_unmap;
		}
		uart->rx_dma_channel = res->start;

		init_timer(&(uart->rx_dma_timer));
#endif

#if defined(CONFIG_SERIAL_BFIN_CTSRTS) || \
	defined(CONFIG_SERIAL_BFIN_HARD_CTSRTS)
		res = platform_get_resource(pdev, IORESOURCE_IO, 0);
		if (res == NULL)
			uart->cts_pin = -1;
		else
			uart->cts_pin = res->start;

		res = platform_get_resource(pdev, IORESOURCE_IO, 1);
		if (res == NULL)
			uart->rts_pin = -1;
		else
			uart->rts_pin = res->start;
# if defined(CONFIG_SERIAL_BFIN_CTSRTS)
		if (uart->rts_pin >= 0)
			gpio_request(uart->rts_pin, DRIVER_NAME);
# endif
#endif
	}

#ifdef CONFIG_SERIAL_BFIN_CONSOLE
	if (!is_early_platform_device(pdev)) {
#endif
		uart = bfin_serial_ports[pdev->id];
		uart->port.dev = &pdev->dev;
		dev_set_drvdata(&pdev->dev, uart);
		ret = uart_add_one_port(&bfin_serial_reg, &uart->port);
#ifdef CONFIG_SERIAL_BFIN_CONSOLE
	}
#endif

	if (!ret)
		return 0;

	if (uart) {
out_error_unmap:
		iounmap(uart->port.membase);
out_error_free_peripherals:
		peripheral_free_list(
			(unsigned short *)pdev->dev.platform_data);
out_error_free_mem:
		kfree(uart);
		bfin_serial_ports[pdev->id] = NULL;
	}

	return ret;
}

static int __devexit bfin_serial_remove(struct platform_device *pdev)
{
	struct bfin_serial_port *uart = platform_get_drvdata(pdev);

	dev_set_drvdata(&pdev->dev, NULL);

	if (uart) {
		uart_remove_one_port(&bfin_serial_reg, &uart->port);
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
		if (uart->rts_pin >= 0)
			gpio_free(uart->rts_pin);
#endif
		iounmap(uart->port.membase);
		peripheral_free_list(
			(unsigned short *)pdev->dev.platform_data);
		kfree(uart);
		bfin_serial_ports[pdev->id] = NULL;
	}

	return 0;
}

static struct platform_driver bfin_serial_driver = {
	.probe		= bfin_serial_probe,
	.remove		= __devexit_p(bfin_serial_remove),
	.suspend	= bfin_serial_suspend,
	.resume		= bfin_serial_resume,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

#if defined(CONFIG_SERIAL_BFIN_CONSOLE)
static __initdata struct early_platform_driver early_bfin_serial_driver = {
	.class_str = CLASS_BFIN_CONSOLE,
	.pdrv = &bfin_serial_driver,
	.requested_id = EARLY_PLATFORM_ID_UNSET,
};

static int __init bfin_serial_rs_console_init(void)
{
	early_platform_driver_register(&early_bfin_serial_driver, DRIVER_NAME);

	early_platform_driver_probe(CLASS_BFIN_CONSOLE, BFIN_UART_NR_PORTS, 0);

	register_console(&bfin_serial_console);

	return 0;
}
console_initcall(bfin_serial_rs_console_init);
#endif

#ifdef CONFIG_EARLY_PRINTK
/*
 * Memory can't be allocated dynamically during earlyprink init stage.
 * So, do individual probe for earlyprink with a static uart port variable.
 */
static int bfin_earlyprintk_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	if (pdev->id < 0 || pdev->id >= BFIN_UART_NR_PORTS) {
		dev_err(&pdev->dev, "Wrong earlyprintk platform device id.\n");
		return -ENOENT;
	}

	ret = peripheral_request_list(
		(unsigned short *)pdev->dev.platform_data, DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev,
				"fail to request bfin serial peripherals\n");
			return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
		ret = -ENOENT;
		goto out_error_free_peripherals;
	}

	bfin_earlyprintk_port.port.membase = ioremap(res->start,
						     resource_size(res));
	if (!bfin_earlyprintk_port.port.membase) {
		dev_err(&pdev->dev, "Cannot map uart IO\n");
		ret = -ENXIO;
		goto out_error_free_peripherals;
	}
	bfin_earlyprintk_port.port.mapbase = res->start;
	bfin_earlyprintk_port.port.line = pdev->id;
	bfin_earlyprintk_port.port.uartclk = get_sclk();
	bfin_earlyprintk_port.port.fifosize  = BFIN_UART_TX_FIFO_SIZE;
	spin_lock_init(&bfin_earlyprintk_port.port.lock);

	return 0;

out_error_free_peripherals:
	peripheral_free_list(
		(unsigned short *)pdev->dev.platform_data);

	return ret;
}

static struct platform_driver bfin_earlyprintk_driver = {
	.probe		= bfin_earlyprintk_probe,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

static __initdata struct early_platform_driver early_bfin_earlyprintk_driver = {
	.class_str = CLASS_BFIN_EARLYPRINTK,
	.pdrv = &bfin_earlyprintk_driver,
	.requested_id = EARLY_PLATFORM_ID_UNSET,
};

struct console __init *bfin_earlyserial_init(unsigned int port,
						unsigned int cflag)
{
	struct ktermios t;
	char port_name[20];

	if (port < 0 || port >= BFIN_UART_NR_PORTS)
		return NULL;

	/*
	 * Only probe resource of the given port in earlyprintk boot arg.
	 * The expected port id should be indicated in port name string.
	 */
	snprintf(port_name, 20, DRIVER_NAME ".%d", port);
	early_platform_driver_register(&early_bfin_earlyprintk_driver,
		port_name);
	early_platform_driver_probe(CLASS_BFIN_EARLYPRINTK, 1, 0);

	if (!bfin_earlyprintk_port.port.membase)
		return NULL;

#ifdef CONFIG_SERIAL_BFIN_CONSOLE
	/*
	 * If we are using early serial, don't let the normal console rewind
	 * log buffer, since that causes things to be printed multiple times
	 */
	bfin_serial_console.flags &= ~CON_PRINTBUFFER;
#endif

	bfin_early_serial_console.index = port;
	t.c_cflag = cflag;
	t.c_iflag = 0;
	t.c_oflag = 0;
	t.c_lflag = ICANON;
	t.c_line = port;
	bfin_serial_set_termios(&bfin_earlyprintk_port.port, &t, &t);

	return &bfin_early_serial_console;
}
#endif /* CONFIG_EARLY_PRINTK */

static int __init bfin_serial_init(void)
{
	int ret;

	pr_info("Blackfin serial driver\n");

	ret = uart_register_driver(&bfin_serial_reg);
	if (ret) {
		pr_err("failed to register %s:%d\n",
			bfin_serial_reg.driver_name, ret);
	}

	ret = platform_driver_register(&bfin_serial_driver);
	if (ret) {
		pr_err("fail to register bfin uart\n");
		uart_unregister_driver(&bfin_serial_reg);
	}

	return ret;
}

static void __exit bfin_serial_exit(void)
{
	platform_driver_unregister(&bfin_serial_driver);
	uart_unregister_driver(&bfin_serial_reg);
}


module_init(bfin_serial_init);
module_exit(bfin_serial_exit);

MODULE_AUTHOR("Sonic Zhang, Aubrey Li");
MODULE_DESCRIPTION("Blackfin generic serial port driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(BFIN_SERIAL_MAJOR);
MODULE_ALIAS("platform:bfin-uart");
