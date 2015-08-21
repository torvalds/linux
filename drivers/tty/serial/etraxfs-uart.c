#include <linux/module.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/tty_flip.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <hwregs/ser_defs.h>

#define DRV_NAME "etraxfs-uart"
#define UART_NR CONFIG_ETRAX_SERIAL_PORTS

#define MODIFY_REG(instance, reg, var)				\
	do {							\
		if (REG_RD_INT(ser, instance, reg) !=		\
		    REG_TYPE_CONV(int, reg_ser_##reg, var))	\
			REG_WR(ser, instance, reg, var);	\
	} while (0)

struct uart_cris_port {
	struct uart_port port;

	int initialized;
	int irq;

	void __iomem *regi_ser;

	struct gpio_desc *dtr_pin;
	struct gpio_desc *dsr_pin;
	struct gpio_desc *ri_pin;
	struct gpio_desc *cd_pin;

	int write_ongoing;
};

static struct uart_driver etraxfs_uart_driver;
static struct uart_port *console_port;
static int console_baud = 115200;
static struct uart_cris_port *etraxfs_uart_ports[UART_NR];

static void cris_serial_port_init(struct uart_port *port, int line);
static void etraxfs_uart_stop_rx(struct uart_port *port);
static inline void etraxfs_uart_start_tx_bottom(struct uart_port *port);

#ifdef CONFIG_SERIAL_ETRAXFS_CONSOLE
static void
cris_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_cris_port *up;
	int i;
	reg_ser_r_stat_din stat;
	reg_ser_rw_tr_dma_en tr_dma_en, old;

	up = etraxfs_uart_ports[co->index];

	if (!up)
		return;

	/* Switch to manual mode. */
	tr_dma_en = old = REG_RD(ser, up->regi_ser, rw_tr_dma_en);
	if (tr_dma_en.en == regk_ser_yes) {
		tr_dma_en.en = regk_ser_no;
		REG_WR(ser, up->regi_ser, rw_tr_dma_en, tr_dma_en);
	}

	/* Send data. */
	for (i = 0; i < count; i++) {
		/* LF -> CRLF */
		if (s[i] == '\n') {
			do {
				stat = REG_RD(ser, up->regi_ser, r_stat_din);
			} while (!stat.tr_rdy);
			REG_WR_INT(ser, up->regi_ser, rw_dout, '\r');
		}
		/* Wait until transmitter is ready and send. */
		do {
			stat = REG_RD(ser, up->regi_ser, r_stat_din);
		} while (!stat.tr_rdy);
		REG_WR_INT(ser, up->regi_ser, rw_dout, s[i]);
	}

	/* Restore mode. */
	if (tr_dma_en.en != old.en)
		REG_WR(ser, up->regi_ser, rw_tr_dma_en, old);
}

static int __init
cris_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= UART_NR)
		co->index = 0;
	port = &etraxfs_uart_ports[co->index]->port;
	console_port = port;

	co->flags |= CON_CONSDEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	console_baud = baud;
	cris_serial_port_init(port, co->index);
	uart_set_options(port, co, baud, parity, bits, flow);

	return 0;
}

static struct tty_driver *cris_console_device(struct console *co, int *index)
{
	struct uart_driver *p = co->data;
	*index = co->index;
	return p->tty_driver;
}

static struct console cris_console = {
	.name = "ttyS",
	.write = cris_console_write,
	.device = cris_console_device,
	.setup = cris_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &etraxfs_uart_driver,
};
#endif /* CONFIG_SERIAL_ETRAXFS_CONSOLE */

static struct uart_driver etraxfs_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "serial",
	.dev_name = "ttyS",
	.major = TTY_MAJOR,
	.minor = 64,
	.nr = UART_NR,
#ifdef CONFIG_SERIAL_ETRAXFS_CONSOLE
	.cons = &cris_console,
#endif /* CONFIG_SERIAL_ETRAXFS_CONSOLE */
};

static inline int crisv32_serial_get_rts(struct uart_cris_port *up)
{
	void __iomem *regi_ser = up->regi_ser;
	/*
	 * Return what the user has controlled rts to or
	 * what the pin is? (if auto_rts is used it differs during tx)
	 */
	reg_ser_r_stat_din rstat = REG_RD(ser, regi_ser, r_stat_din);

	return !(rstat.rts_n == regk_ser_active);
}

/*
 * A set = 0 means 3.3V on the pin, bitvalue: 0=active, 1=inactive
 *                                            0=0V    , 1=3.3V
 */
static inline void crisv32_serial_set_rts(struct uart_cris_port *up,
					  int set, int force)
{
	void __iomem *regi_ser = up->regi_ser;

	unsigned long flags;
	reg_ser_rw_rec_ctrl rec_ctrl;

	local_irq_save(flags);
	rec_ctrl = REG_RD(ser, regi_ser, rw_rec_ctrl);

	if (set)
		rec_ctrl.rts_n = regk_ser_active;
	else
		rec_ctrl.rts_n = regk_ser_inactive;
	REG_WR(ser, regi_ser, rw_rec_ctrl, rec_ctrl);
	local_irq_restore(flags);
}

static inline int crisv32_serial_get_cts(struct uart_cris_port *up)
{
	void __iomem *regi_ser = up->regi_ser;
	reg_ser_r_stat_din rstat = REG_RD(ser, regi_ser, r_stat_din);

	return (rstat.cts_n == regk_ser_active);
}

/*
 * Send a single character for XON/XOFF purposes.  We do it in this separate
 * function instead of the alternative support port.x_char, in the ...start_tx
 * function, so we don't mix up this case with possibly enabling transmission
 * of queued-up data (in case that's disabled after *receiving* an XOFF or
 * negative CTS).  This function is used for both DMA and non-DMA case; see HW
 * docs specifically blessing sending characters manually when DMA for
 * transmission is enabled and running.  We may be asked to transmit despite
 * the transmitter being disabled by a ..._stop_tx call so we need to enable
 * it temporarily but restore the state afterwards.
 */
static void etraxfs_uart_send_xchar(struct uart_port *port, char ch)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	reg_ser_rw_dout dout = { .data = ch };
	reg_ser_rw_ack_intr ack_intr = { .tr_rdy = regk_ser_yes };
	reg_ser_r_stat_din rstat;
	reg_ser_rw_tr_ctrl prev_tr_ctrl, tr_ctrl;
	void __iomem *regi_ser = up->regi_ser;
	unsigned long flags;

	/*
	 * Wait for tr_rdy in case a character is already being output.  Make
	 * sure we have integrity between the register reads and the writes
	 * below, but don't busy-wait with interrupts off and the port lock
	 * taken.
	 */
	spin_lock_irqsave(&port->lock, flags);
	do {
		spin_unlock_irqrestore(&port->lock, flags);
		spin_lock_irqsave(&port->lock, flags);
		prev_tr_ctrl = tr_ctrl = REG_RD(ser, regi_ser, rw_tr_ctrl);
		rstat = REG_RD(ser, regi_ser, r_stat_din);
	} while (!rstat.tr_rdy);

	/*
	 * Ack an interrupt if one was just issued for the previous character
	 * that was output.  This is required for non-DMA as the interrupt is
	 * used as the only indicator that the transmitter is ready and it
	 * isn't while this x_char is being transmitted.
	 */
	REG_WR(ser, regi_ser, rw_ack_intr, ack_intr);

	/* Enable the transmitter in case it was disabled. */
	tr_ctrl.stop = 0;
	REG_WR(ser, regi_ser, rw_tr_ctrl, tr_ctrl);

	/*
	 * Finally, send the blessed character; nothing should stop it now,
	 * except for an xoff-detected state, which we'll handle below.
	 */
	REG_WR(ser, regi_ser, rw_dout, dout);
	up->port.icount.tx++;

	/* There might be an xoff state to clear. */
	rstat = REG_RD(ser, up->regi_ser, r_stat_din);

	/*
	 * Clear any xoff state that *may* have been there to
	 * inhibit transmission of the character.
	 */
	if (rstat.xoff_detect) {
		reg_ser_rw_xoff_clr xoff_clr = { .clr = 1 };
		reg_ser_rw_tr_dma_en tr_dma_en;

		REG_WR(ser, regi_ser, rw_xoff_clr, xoff_clr);
		tr_dma_en = REG_RD(ser, regi_ser, rw_tr_dma_en);

		/*
		 * If we had an xoff state but cleared it, instead sneak in a
		 * disabled state for the transmitter, after the character we
		 * sent.  Thus we keep the port disabled, just as if the xoff
		 * state was still in effect (or actually, as if stop_tx had
		 * been called, as we stop DMA too).
		 */
		prev_tr_ctrl.stop = 1;

		tr_dma_en.en = 0;
		REG_WR(ser, regi_ser, rw_tr_dma_en, tr_dma_en);
	}

	/* Restore "previous" enabled/disabled state of the transmitter. */
	REG_WR(ser, regi_ser, rw_tr_ctrl, prev_tr_ctrl);

	spin_unlock_irqrestore(&port->lock, flags);
}

/*
 * Do not spin_lock_irqsave or disable interrupts by other means here; it's
 * already done by the caller.
 */
static void etraxfs_uart_start_tx(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;

	/* we have already done below if a write is ongoing */
	if (up->write_ongoing)
		return;

	/* Signal that write is ongoing */
	up->write_ongoing = 1;

	etraxfs_uart_start_tx_bottom(port);
}

static inline void etraxfs_uart_start_tx_bottom(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	void __iomem *regi_ser = up->regi_ser;
	reg_ser_rw_tr_ctrl tr_ctrl;
	reg_ser_rw_intr_mask intr_mask;

	tr_ctrl = REG_RD(ser, regi_ser, rw_tr_ctrl);
	tr_ctrl.stop = regk_ser_no;
	REG_WR(ser, regi_ser, rw_tr_ctrl, tr_ctrl);
	intr_mask = REG_RD(ser, regi_ser, rw_intr_mask);
	intr_mask.tr_rdy = regk_ser_yes;
	REG_WR(ser, regi_ser, rw_intr_mask, intr_mask);
}

/*
 * This function handles both the DMA and non-DMA case by ordering the
 * transmitter to stop of after the current character.  We don't need to wait
 * for any such character to be completely transmitted; we do that where it
 * matters, like in etraxfs_uart_set_termios.  Don't busy-wait here; see
 * Documentation/serial/driver: this function is called within
 * spin_lock_irq{,save} and thus separate ones would be disastrous (when SMP).
 * There's no documented need to set the txd pin to any particular value;
 * break setting is controlled solely by etraxfs_uart_break_ctl.
 */
static void etraxfs_uart_stop_tx(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	void __iomem *regi_ser = up->regi_ser;
	reg_ser_rw_tr_ctrl tr_ctrl;
	reg_ser_rw_intr_mask intr_mask;
	reg_ser_rw_tr_dma_en tr_dma_en = {0};
	reg_ser_rw_xoff_clr xoff_clr = {0};

	/*
	 * For the non-DMA case, we'd get a tr_rdy interrupt that we're not
	 * interested in as we're not transmitting any characters.  For the
	 * DMA case, that interrupt is already turned off, but no reason to
	 * waste code on conditionals here.
	 */
	intr_mask = REG_RD(ser, regi_ser, rw_intr_mask);
	intr_mask.tr_rdy = regk_ser_no;
	REG_WR(ser, regi_ser, rw_intr_mask, intr_mask);

	tr_ctrl = REG_RD(ser, regi_ser, rw_tr_ctrl);
	tr_ctrl.stop = 1;
	REG_WR(ser, regi_ser, rw_tr_ctrl, tr_ctrl);

	/*
	 * Always clear possible hardware xoff-detected state here, no need to
	 * unnecessary consider mctrl settings and when they change.  We clear
	 * it here rather than in start_tx: both functions are called as the
	 * effect of XOFF processing, but start_tx is also called when upper
	 * levels tell the driver that there are more characters to send, so
	 * avoid adding code there.
	 */
	xoff_clr.clr = 1;
	REG_WR(ser, regi_ser, rw_xoff_clr, xoff_clr);

	/*
	 * Disable transmitter DMA, so that if we're in XON/XOFF, we can send
	 * those single characters without also giving go-ahead for queued up
	 * DMA data.
	 */
	tr_dma_en.en = 0;
	REG_WR(ser, regi_ser, rw_tr_dma_en, tr_dma_en);

	/*
	 * Make sure that write_ongoing is reset when stopping tx.
	 */
	up->write_ongoing = 0;
}

static void etraxfs_uart_stop_rx(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	void __iomem *regi_ser = up->regi_ser;
	reg_ser_rw_rec_ctrl rec_ctrl = REG_RD(ser, regi_ser, rw_rec_ctrl);

	rec_ctrl.en = regk_ser_no;
	REG_WR(ser, regi_ser, rw_rec_ctrl, rec_ctrl);
}

static void etraxfs_uart_enable_ms(struct uart_port *port)
{
}

static void check_modem_status(struct uart_cris_port *up)
{
}

static unsigned int etraxfs_uart_tx_empty(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	unsigned long flags;
	unsigned int ret;
	reg_ser_r_stat_din rstat = {0};

	spin_lock_irqsave(&up->port.lock, flags);

	rstat = REG_RD(ser, up->regi_ser, r_stat_din);
	ret = rstat.tr_empty ? TIOCSER_TEMT : 0;

	spin_unlock_irqrestore(&up->port.lock, flags);
	return ret;
}
static unsigned int etraxfs_uart_get_mctrl(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	unsigned int ret;

	ret = 0;
	if (crisv32_serial_get_rts(up))
		ret |= TIOCM_RTS;
	/* DTR is active low */
	if (up->dtr_pin && !gpiod_get_raw_value(up->dtr_pin))
		ret |= TIOCM_DTR;
	/* CD is active low */
	if (up->cd_pin && !gpiod_get_raw_value(up->cd_pin))
		ret |= TIOCM_CD;
	/* RI is active low */
	if (up->ri_pin && !gpiod_get_raw_value(up->ri_pin))
		ret |= TIOCM_RI;
	/* DSR is active low */
	if (up->dsr_pin && !gpiod_get_raw_value(up->dsr_pin))
		ret |= TIOCM_DSR;
	if (crisv32_serial_get_cts(up))
		ret |= TIOCM_CTS;
	return ret;
}

static void etraxfs_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;

	crisv32_serial_set_rts(up, mctrl & TIOCM_RTS ? 1 : 0, 0);
	/* DTR is active low */
	if (up->dtr_pin)
		gpiod_set_raw_value(up->dtr_pin, mctrl & TIOCM_DTR ? 0 : 1);
	/* RI is active low */
	if (up->ri_pin)
		gpiod_set_raw_value(up->ri_pin, mctrl & TIOCM_RNG ? 0 : 1);
	/* CD is active low */
	if (up->cd_pin)
		gpiod_set_raw_value(up->cd_pin, mctrl & TIOCM_CD ? 0 : 1);
}

static void etraxfs_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	unsigned long flags;
	reg_ser_rw_tr_ctrl tr_ctrl;
	reg_ser_rw_tr_dma_en tr_dma_en;
	reg_ser_rw_intr_mask intr_mask;

	spin_lock_irqsave(&up->port.lock, flags);
	tr_ctrl = REG_RD(ser, up->regi_ser, rw_tr_ctrl);
	tr_dma_en = REG_RD(ser, up->regi_ser, rw_tr_dma_en);
	intr_mask = REG_RD(ser, up->regi_ser, rw_intr_mask);

	if (break_state != 0) { /* Send break */
		/*
		 * We need to disable DMA (if used) or tr_rdy interrupts if no
		 * DMA.  No need to make this conditional on use of DMA;
		 * disabling will be a no-op for the other mode.
		 */
		intr_mask.tr_rdy = regk_ser_no;
		tr_dma_en.en = 0;

		/*
		 * Stop transmission and set the txd pin to 0 after the
		 * current character.  The txd setting will take effect after
		 * any current transmission has completed.
		 */
		tr_ctrl.stop = 1;
		tr_ctrl.txd = 0;
	} else {
		/* Re-enable the serial interrupt. */
		intr_mask.tr_rdy = regk_ser_yes;

		tr_ctrl.stop = 0;
		tr_ctrl.txd = 1;
	}
	REG_WR(ser, up->regi_ser, rw_tr_ctrl, tr_ctrl);
	REG_WR(ser, up->regi_ser, rw_tr_dma_en, tr_dma_en);
	REG_WR(ser, up->regi_ser, rw_intr_mask, intr_mask);

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void
transmit_chars_no_dma(struct uart_cris_port *up)
{
	int max_count;
	struct circ_buf *xmit = &up->port.state->xmit;

	void __iomem *regi_ser = up->regi_ser;
	reg_ser_r_stat_din rstat;
	reg_ser_rw_ack_intr ack_intr = { .tr_rdy = regk_ser_yes };

	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		/* No more to send, so disable the interrupt. */
		reg_ser_rw_intr_mask intr_mask;

		intr_mask = REG_RD(ser, regi_ser, rw_intr_mask);
		intr_mask.tr_rdy = 0;
		intr_mask.tr_empty = 0;
		REG_WR(ser, regi_ser, rw_intr_mask, intr_mask);
		up->write_ongoing = 0;
		return;
	}

	/* If the serport is fast, we send up to max_count bytes before
	   exiting the loop.  */
	max_count = 64;
	do {
		reg_ser_rw_dout dout = { .data = xmit->buf[xmit->tail] };

		REG_WR(ser, regi_ser, rw_dout, dout);
		REG_WR(ser, regi_ser, rw_ack_intr, ack_intr);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE-1);
		up->port.icount.tx++;
		if (xmit->head == xmit->tail)
			break;
		rstat = REG_RD(ser, regi_ser, r_stat_din);
	} while ((--max_count > 0) && rstat.tr_rdy);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);
}

static void receive_chars_no_dma(struct uart_cris_port *up)
{
	reg_ser_rs_stat_din stat_din;
	reg_ser_r_stat_din rstat;
	struct tty_port *port;
	struct uart_icount *icount;
	int max_count = 16;
	char flag;
	reg_ser_rw_ack_intr ack_intr = { 0 };

	rstat = REG_RD(ser, up->regi_ser, r_stat_din);
	icount = &up->port.icount;
	port = &up->port.state->port;

	do {
		stat_din = REG_RD(ser, up->regi_ser, rs_stat_din);

		flag = TTY_NORMAL;
		ack_intr.dav = 1;
		REG_WR(ser, up->regi_ser, rw_ack_intr, ack_intr);
		icount->rx++;

		if (stat_din.framing_err | stat_din.par_err | stat_din.orun) {
			if (stat_din.data == 0x00 &&
			    stat_din.framing_err) {
				/* Most likely a break. */
				flag = TTY_BREAK;
				icount->brk++;
			} else if (stat_din.par_err) {
				flag = TTY_PARITY;
				icount->parity++;
			} else if (stat_din.orun) {
				flag = TTY_OVERRUN;
				icount->overrun++;
			} else if (stat_din.framing_err) {
				flag = TTY_FRAME;
				icount->frame++;
			}
		}

		/*
		 * If this becomes important, we probably *could* handle this
		 * gracefully by keeping track of the unhandled character.
		 */
		if (!tty_insert_flip_char(port, stat_din.data, flag))
			panic("%s: No tty buffer space", __func__);
		rstat = REG_RD(ser, up->regi_ser, r_stat_din);
	} while (rstat.dav && (max_count-- > 0));
	spin_unlock(&up->port.lock);
	tty_flip_buffer_push(port);
	spin_lock(&up->port.lock);
}

static irqreturn_t
ser_interrupt(int irq, void *dev_id)
{
	struct uart_cris_port *up = (struct uart_cris_port *)dev_id;
	void __iomem *regi_ser;
	int handled = 0;

	spin_lock(&up->port.lock);

	regi_ser = up->regi_ser;

	if (regi_ser) {
		reg_ser_r_masked_intr masked_intr;

		masked_intr = REG_RD(ser, regi_ser, r_masked_intr);
		/*
		 * Check what interrupts are active before taking
		 * actions. If DMA is used the interrupt shouldn't
		 * be enabled.
		 */
		if (masked_intr.dav) {
			receive_chars_no_dma(up);
			handled = 1;
		}
		check_modem_status(up);

		if (masked_intr.tr_rdy) {
			transmit_chars_no_dma(up);
			handled = 1;
		}
	}
	spin_unlock(&up->port.lock);
	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_CONSOLE_POLL
static int etraxfs_uart_get_poll_char(struct uart_port *port)
{
	reg_ser_rs_stat_din stat;
	reg_ser_rw_ack_intr ack_intr = { 0 };
	struct uart_cris_port *up = (struct uart_cris_port *)port;

	do {
		stat = REG_RD(ser, up->regi_ser, rs_stat_din);
	} while (!stat.dav);

	/* Ack the data_avail interrupt. */
	ack_intr.dav = 1;
	REG_WR(ser, up->regi_ser, rw_ack_intr, ack_intr);

	return stat.data;
}

static void etraxfs_uart_put_poll_char(struct uart_port *port,
					unsigned char c)
{
	reg_ser_r_stat_din stat;
	struct uart_cris_port *up = (struct uart_cris_port *)port;

	do {
		stat = REG_RD(ser, up->regi_ser, r_stat_din);
	} while (!stat.tr_rdy);
	REG_WR_INT(ser, up->regi_ser, rw_dout, c);
}
#endif /* CONFIG_CONSOLE_POLL */

static int etraxfs_uart_startup(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	unsigned long flags;
	reg_ser_rw_intr_mask ser_intr_mask = {0};

	ser_intr_mask.dav = regk_ser_yes;

	if (request_irq(etraxfs_uart_ports[port->line]->irq, ser_interrupt,
			0, DRV_NAME, etraxfs_uart_ports[port->line]))
		panic("irq ser%d", port->line);

	spin_lock_irqsave(&up->port.lock, flags);

	REG_WR(ser, up->regi_ser, rw_intr_mask, ser_intr_mask);

	etraxfs_uart_set_mctrl(&up->port, up->port.mctrl);

	spin_unlock_irqrestore(&up->port.lock, flags);

	return 0;
}

static void etraxfs_uart_shutdown(struct uart_port *port)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);

	etraxfs_uart_stop_tx(port);
	etraxfs_uart_stop_rx(port);

	free_irq(etraxfs_uart_ports[port->line]->irq,
		 etraxfs_uart_ports[port->line]);

	etraxfs_uart_set_mctrl(&up->port, up->port.mctrl);

	spin_unlock_irqrestore(&up->port.lock, flags);

}

static void
etraxfs_uart_set_termios(struct uart_port *port, struct ktermios *termios,
			 struct ktermios *old)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;
	unsigned long flags;
	reg_ser_rw_xoff xoff;
	reg_ser_rw_xoff_clr xoff_clr = {0};
	reg_ser_rw_tr_ctrl tx_ctrl = {0};
	reg_ser_rw_tr_dma_en tx_dma_en = {0};
	reg_ser_rw_rec_ctrl rx_ctrl = {0};
	reg_ser_rw_tr_baud_div tx_baud_div = {0};
	reg_ser_rw_rec_baud_div rx_baud_div = {0};
	int baud;

	if (old &&
	    termios->c_cflag == old->c_cflag &&
	    termios->c_iflag == old->c_iflag)
		return;

	/* Tx: 8 bit, no/even parity, 1 stop bit, no cts. */
	tx_ctrl.base_freq = regk_ser_f29_493;
	tx_ctrl.en = 0;
	tx_ctrl.stop = 0;
	tx_ctrl.auto_rts = regk_ser_no;
	tx_ctrl.txd = 1;
	tx_ctrl.auto_cts = 0;
	/* Rx: 8 bit, no/even parity. */
	rx_ctrl.dma_err = regk_ser_stop;
	rx_ctrl.sampling = regk_ser_majority;
	rx_ctrl.timeout = 1;

	rx_ctrl.rts_n = regk_ser_inactive;

	/* Common for tx and rx: 8N1. */
	tx_ctrl.data_bits = regk_ser_bits8;
	rx_ctrl.data_bits = regk_ser_bits8;
	tx_ctrl.par = regk_ser_even;
	rx_ctrl.par = regk_ser_even;
	tx_ctrl.par_en = regk_ser_no;
	rx_ctrl.par_en = regk_ser_no;

	tx_ctrl.stop_bits = regk_ser_bits1;

	/*
	 * Change baud-rate and write it to the hardware.
	 *
	 * baud_clock = base_freq / (divisor*8)
	 * divisor = base_freq / (baud_clock * 8)
	 * base_freq is either:
	 * off, ext, 29.493MHz, 32.000 MHz, 32.768 MHz or 100 MHz
	 * 20.493MHz is used for standard baudrates
	 */

	/*
	 * For the console port we keep the original baudrate here.  Not very
	 * beautiful.
	 */
	if ((port != console_port) || old)
		baud = uart_get_baud_rate(port, termios, old, 0,
					  port->uartclk / 8);
	else
		baud = console_baud;

	tx_baud_div.div = 29493000 / (8 * baud);
	/* Rx uses same as tx. */
	rx_baud_div.div = tx_baud_div.div;
	rx_ctrl.base_freq = tx_ctrl.base_freq;

	if ((termios->c_cflag & CSIZE) == CS7) {
		/* Set 7 bit mode. */
		tx_ctrl.data_bits = regk_ser_bits7;
		rx_ctrl.data_bits = regk_ser_bits7;
	}

	if (termios->c_cflag & CSTOPB) {
		/* Set 2 stop bit mode. */
		tx_ctrl.stop_bits = regk_ser_bits2;
	}

	if (termios->c_cflag & PARENB) {
		/* Enable parity. */
		tx_ctrl.par_en = regk_ser_yes;
		rx_ctrl.par_en = regk_ser_yes;
	}

	if (termios->c_cflag & CMSPAR) {
		if (termios->c_cflag & PARODD) {
			/* Set mark parity if PARODD and CMSPAR. */
			tx_ctrl.par = regk_ser_mark;
			rx_ctrl.par = regk_ser_mark;
		} else {
			tx_ctrl.par = regk_ser_space;
			rx_ctrl.par = regk_ser_space;
		}
	} else {
		if (termios->c_cflag & PARODD) {
			/* Set odd parity. */
		       tx_ctrl.par = regk_ser_odd;
		       rx_ctrl.par = regk_ser_odd;
		}
	}

	if (termios->c_cflag & CRTSCTS) {
		/* Enable automatic CTS handling. */
		tx_ctrl.auto_cts = regk_ser_yes;
	}

	/* Make sure the tx and rx are enabled. */
	tx_ctrl.en = regk_ser_yes;
	rx_ctrl.en = regk_ser_yes;

	spin_lock_irqsave(&port->lock, flags);

	tx_dma_en.en = 0;
	REG_WR(ser, up->regi_ser, rw_tr_dma_en, tx_dma_en);

	/* Actually write the control regs (if modified) to the hardware. */
	uart_update_timeout(port, termios->c_cflag, port->uartclk/8);
	MODIFY_REG(up->regi_ser, rw_rec_baud_div, rx_baud_div);
	MODIFY_REG(up->regi_ser, rw_rec_ctrl, rx_ctrl);

	MODIFY_REG(up->regi_ser, rw_tr_baud_div, tx_baud_div);
	MODIFY_REG(up->regi_ser, rw_tr_ctrl, tx_ctrl);

	tx_dma_en.en = 0;
	REG_WR(ser, up->regi_ser, rw_tr_dma_en, tx_dma_en);

	xoff = REG_RD(ser, up->regi_ser, rw_xoff);

	if (up->port.state && up->port.state->port.tty &&
	    (up->port.state->port.tty->termios.c_iflag & IXON)) {
		xoff.chr = STOP_CHAR(up->port.state->port.tty);
		xoff.automatic = regk_ser_yes;
	} else
		xoff.automatic = regk_ser_no;

	MODIFY_REG(up->regi_ser, rw_xoff, xoff);

	/*
	 * Make sure we don't start in an automatically shut-off state due to
	 * a previous early exit.
	 */
	xoff_clr.clr = 1;
	REG_WR(ser, up->regi_ser, rw_xoff_clr, xoff_clr);

	etraxfs_uart_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static const char *
etraxfs_uart_type(struct uart_port *port)
{
	return "CRISv32";
}

static void etraxfs_uart_release_port(struct uart_port *port)
{
}

static int etraxfs_uart_request_port(struct uart_port *port)
{
	return 0;
}

static void etraxfs_uart_config_port(struct uart_port *port, int flags)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;

	up->port.type = PORT_CRIS;
}

static const struct uart_ops etraxfs_uart_pops = {
	.tx_empty = etraxfs_uart_tx_empty,
	.set_mctrl = etraxfs_uart_set_mctrl,
	.get_mctrl = etraxfs_uart_get_mctrl,
	.stop_tx = etraxfs_uart_stop_tx,
	.start_tx = etraxfs_uart_start_tx,
	.send_xchar = etraxfs_uart_send_xchar,
	.stop_rx = etraxfs_uart_stop_rx,
	.enable_ms = etraxfs_uart_enable_ms,
	.break_ctl = etraxfs_uart_break_ctl,
	.startup = etraxfs_uart_startup,
	.shutdown = etraxfs_uart_shutdown,
	.set_termios = etraxfs_uart_set_termios,
	.type = etraxfs_uart_type,
	.release_port = etraxfs_uart_release_port,
	.request_port = etraxfs_uart_request_port,
	.config_port = etraxfs_uart_config_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = etraxfs_uart_get_poll_char,
	.poll_put_char = etraxfs_uart_put_poll_char,
#endif
};

static void cris_serial_port_init(struct uart_port *port, int line)
{
	struct uart_cris_port *up = (struct uart_cris_port *)port;

	if (up->initialized)
		return;
	up->initialized = 1;
	port->line = line;
	spin_lock_init(&port->lock);
	port->ops = &etraxfs_uart_pops;
	port->irq = up->irq;
	port->iobase = (unsigned long) up->regi_ser;
	port->uartclk = 29493000;

	/*
	 * We can't fit any more than 255 here (unsigned char), though
	 * actually UART_XMIT_SIZE characters could be pending output.
	 * At time of this writing, the definition of "fifosize" is here the
	 * amount of characters that can be pending output after a start_tx call
	 * until tx_empty returns 1: see serial_core.c:uart_wait_until_sent.
	 * This matters for timeout calculations unfortunately, but keeping
	 * larger amounts at the DMA wouldn't win much so let's just play nice.
	 */
	port->fifosize = 255;
	port->flags = UPF_BOOT_AUTOCONF;
}

static int etraxfs_uart_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct uart_cris_port *up;
	int dev_id;

	if (!np)
		return -ENODEV;

	dev_id = of_alias_get_id(np, "serial");
	if (dev_id < 0)
		dev_id = 0;

	if (dev_id >= UART_NR)
		return -EINVAL;

	if (etraxfs_uart_ports[dev_id])
		return -EBUSY;

	up = devm_kzalloc(&pdev->dev, sizeof(struct uart_cris_port),
			  GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	up->irq = irq_of_parse_and_map(np, 0);
	up->regi_ser = of_iomap(np, 0);
	up->dtr_pin = devm_gpiod_get_optional(&pdev->dev, "dtr");
	up->dsr_pin = devm_gpiod_get_optional(&pdev->dev, "dsr");
	up->ri_pin = devm_gpiod_get_optional(&pdev->dev, "ri");
	up->cd_pin = devm_gpiod_get_optional(&pdev->dev, "cd");
	up->port.dev = &pdev->dev;
	cris_serial_port_init(&up->port, dev_id);

	etraxfs_uart_ports[dev_id] = up;
	platform_set_drvdata(pdev, &up->port);
	uart_add_one_port(&etraxfs_uart_driver, &up->port);

	return 0;
}

static int etraxfs_uart_remove(struct platform_device *pdev)
{
	struct uart_port *port;

	port = platform_get_drvdata(pdev);
	uart_remove_one_port(&etraxfs_uart_driver, port);
	etraxfs_uart_ports[port->line] = NULL;

	return 0;
}

static const struct of_device_id etraxfs_uart_dt_ids[] = {
	{ .compatible = "axis,etraxfs-uart" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, etraxfs_uart_dt_ids);

static struct platform_driver etraxfs_uart_platform_driver = {
	.driver = {
		.name   = DRV_NAME,
		.of_match_table	= of_match_ptr(etraxfs_uart_dt_ids),
	},
	.probe          = etraxfs_uart_probe,
	.remove         = etraxfs_uart_remove,
};

static int __init etraxfs_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&etraxfs_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&etraxfs_uart_platform_driver);
	if (ret)
		uart_unregister_driver(&etraxfs_uart_driver);

	return ret;
}

static void __exit etraxfs_uart_exit(void)
{
	platform_driver_unregister(&etraxfs_uart_platform_driver);
	uart_unregister_driver(&etraxfs_uart_driver);
}

module_init(etraxfs_uart_init);
module_exit(etraxfs_uart_exit);
