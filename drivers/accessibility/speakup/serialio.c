// SPDX-License-Identifier: GPL-2.0
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include "spk_types.h"
#include "speakup.h"
#include "spk_priv.h"
#include "serialio.h"

#include <linux/serial_core.h>
#include <linux/serial.h>

static int timeouts;

static int spk_serial_out(struct spk_synth *in_synth, const char ch);
static void spk_serial_send_xchar(struct spk_synth *in_synth, char ch);
static void spk_serial_tiocmset(struct spk_synth *in_synth, unsigned int set, unsigned int clear);
static unsigned char spk_serial_in(struct spk_synth *in_synth);
static unsigned char spk_serial_in_nowait(struct spk_synth *in_synth);
static void spk_serial_flush_buffer(struct spk_synth *in_synth);
static int spk_serial_wait_for_xmitr(struct spk_synth *in_synth);

struct spk_io_ops spk_serial_io_ops = {
	.synth_out = spk_serial_out,
	.send_xchar = spk_serial_send_xchar,
	.tiocmset = spk_serial_tiocmset,
	.synth_in = spk_serial_in,
	.synth_in_nowait = spk_serial_in_nowait,
	.flush_buffer = spk_serial_flush_buffer,
	.wait_for_xmitr = spk_serial_wait_for_xmitr,
};
EXPORT_SYMBOL_GPL(spk_serial_io_ops);

static void spk_serial_send_xchar(struct spk_synth *synth, char ch)
{
	int timeout = SPK_XMITR_TIMEOUT;

	while (spk_serial_tx_busy()) {
		if (!--timeout)
			break;
		udelay(1);
	}
	outb(ch, speakup_info.port_tts);
}

static void spk_serial_tiocmset(struct spk_synth *in_synth, unsigned int set, unsigned int clear)
{
	int old = inb(speakup_info.port_tts + UART_MCR);

	outb((old & ~clear) | set, speakup_info.port_tts + UART_MCR);
}

static int spk_serial_wait_for_xmitr(struct spk_synth *in_synth)
{
	int tmout = SPK_XMITR_TIMEOUT;

	if ((in_synth->alive) && (timeouts >= NUM_DISABLE_TIMEOUTS)) {
		pr_warn("%s: too many timeouts, deactivating speakup\n",
			in_synth->long_name);
		in_synth->alive = 0;
		/* No synth any more, so nobody will restart TTYs, and we thus
		 * need to do it ourselves.  Now that there is no synth we can
		 * let application flood anyway
		 */
		speakup_start_ttys();
		timeouts = 0;
		return 0;
	}
	while (spk_serial_tx_busy()) {
		if (--tmout == 0) {
			pr_warn("%s: timed out (tx busy)\n",
				in_synth->long_name);
			timeouts++;
			return 0;
		}
		udelay(1);
	}
	tmout = SPK_CTS_TIMEOUT;
	while (!((inb_p(speakup_info.port_tts + UART_MSR)) & UART_MSR_CTS)) {
		/* CTS */
		if (--tmout == 0) {
			timeouts++;
			return 0;
		}
		udelay(1);
	}
	timeouts = 0;
	return 1;
}

static unsigned char spk_serial_in(struct spk_synth *in_synth)
{
	int tmout = SPK_SERIAL_TIMEOUT;

	while (!(inb_p(speakup_info.port_tts + UART_LSR) & UART_LSR_DR)) {
		if (--tmout == 0) {
			pr_warn("time out while waiting for input.\n");
			return 0xff;
		}
		udelay(1);
	}
	return inb_p(speakup_info.port_tts + UART_RX);
}

static unsigned char spk_serial_in_nowait(struct spk_synth *in_synth)
{
	unsigned char lsr;

	lsr = inb_p(speakup_info.port_tts + UART_LSR);
	if (!(lsr & UART_LSR_DR))
		return 0;
	return inb_p(speakup_info.port_tts + UART_RX);
}

static void spk_serial_flush_buffer(struct spk_synth *in_synth)
{
	/* TODO: flush the UART 16550 buffer */
}

static int spk_serial_out(struct spk_synth *in_synth, const char ch)
{
	if (in_synth->alive && spk_serial_wait_for_xmitr(in_synth)) {
		outb_p(ch, speakup_info.port_tts);
		return 1;
	}
	return 0;
}
