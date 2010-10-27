/*
 *  max3110.c - spi uart protocol driver for Maxim 3110 on Moorestown
 *
 *  Copyright (C) Intel 2008 Feng Tang <feng.tang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Note:
 * 1. From Max3110 spec, the Rx FIFO has 8 words, while the Tx FIFO only has
 *    1 word. If SPI master controller doesn't support sclk frequency change,
 *    then the char need be sent out one by one with some delay
 *
 * 2. Currently only RX availabe interrrupt is used, no need for waiting TXE
 *    interrupt for a low speed UART device
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

#include <linux/kthread.h>
#include <linux/delay.h>
#include <asm/atomic.h>
#include <linux/spi/spi.h>
#include <linux/spi/dw_spi.h>

#include "mrst_max3110.h"

#define PR_FMT	"mrst_max3110: "

#define UART_TX_NEEDED 1
#define CON_TX_NEEDED  2
#define BIT_IRQ_PENDING    3

struct uart_max3110 {
	struct uart_port port;
	struct spi_device *spi;
	char *name;

	wait_queue_head_t wq;
	struct task_struct *main_thread;
	struct task_struct *read_thread;
	struct mutex thread_mutex;;

	u32 baud;
	u16 cur_conf;
	u8 clock;
	u8 parity, word_7bits;

	unsigned long uart_flags;

	/* console related */
	struct circ_buf con_xmit;

	/* irq related */
	u16 irq;
};

/* global data structure, may need be removed */
struct uart_max3110 *pmax;
static inline void receive_char(struct uart_max3110 *max, u8 ch);
static void receive_chars(struct uart_max3110 *max,
				unsigned char *str, int len);
static int max3110_read_multi(struct uart_max3110 *max, int len, u8 *buf);
static void max3110_console_receive(struct uart_max3110 *max);

int max3110_write_then_read(struct uart_max3110 *max,
		const u8 *txbuf, u8 *rxbuf, unsigned len, int always_fast)
{
	struct spi_device *spi = max->spi;
	struct spi_message	message;
	struct spi_transfer	x;
	int ret;

	if (!txbuf || !rxbuf)
		return -EINVAL;

	spi_message_init(&message);
	memset(&x, 0, sizeof x);
	x.len = len;
	x.tx_buf = txbuf;
	x.rx_buf = rxbuf;
	spi_message_add_tail(&x, &message);

	if (always_fast)
		x.speed_hz = 3125000;
	else if (max->baud)
		x.speed_hz = max->baud;

	/* Do the i/o */
	ret = spi_sync(spi, &message);
	return ret;
}

/* Write a u16 to the device, and return one u16 read back */
int max3110_out(struct uart_max3110 *max, const u16 out)
{
	u16 tmp;
	int ret;

	ret = max3110_write_then_read(max, (u8 *)&out, (u8 *)&tmp, 2, 1);
	if (ret)
		return ret;

	/* If some valid data is read back */
	if (tmp & MAX3110_READ_DATA_AVAILABLE)
		receive_char(max, (tmp & 0xff));

	return ret;
}

#define MAX_READ_LEN	20
/*
 * This is usually used to read data from SPIC RX FIFO, which doesn't
 * need any delay like flushing character out. It returns how many
 * valide bytes are read back
 */
static int max3110_read_multi(struct uart_max3110 *max, int len, u8 *buf)
{
	u16 out[MAX_READ_LEN], in[MAX_READ_LEN];
	u8 *pbuf, valid_str[MAX_READ_LEN];
	int i, j, bytelen;

	if (len > MAX_READ_LEN) {
		pr_err(PR_FMT "read len %d is too large\n", len);
		return 0;
	}

	bytelen = len * 2;
	memset(out, 0, bytelen);
	memset(in, 0, bytelen);

	if (max3110_write_then_read(max, (u8 *)out, (u8 *)in, bytelen, 1))
		return 0;

	/* If caller don't provide a buffer, then handle received char */
	pbuf = buf ? buf : valid_str;

	for (i = 0, j = 0; i < len; i++) {
		if (in[i] & MAX3110_READ_DATA_AVAILABLE)
			pbuf[j++] = (u8)(in[i] & 0xff);
	}

	if (j && (pbuf == valid_str))
		receive_chars(max, valid_str, j);

	return j;
}

static void serial_m3110_con_putchar(struct uart_port *port, int ch)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	struct circ_buf *xmit = &max->con_xmit;

	if (uart_circ_chars_free(xmit)) {
		xmit->buf[xmit->head] = (char)ch;
		xmit->head = (xmit->head + 1) & (PAGE_SIZE - 1);
	}


	if (!test_and_set_bit(CON_TX_NEEDED, &max->uart_flags))
		wake_up_process(max->main_thread);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void serial_m3110_con_write(struct console *co,
				const char *s, unsigned int count)
{
	if (!pmax)
		return;

	uart_console_write(&pmax->port, s, count, serial_m3110_con_putchar);
}

static int __init
serial_m3110_con_setup(struct console *co, char *options)
{
	struct uart_max3110 *max = pmax;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	pr_info(PR_FMT "setting up console\n");

	if (!max) {
		pr_err(PR_FMT "pmax is NULL, return");
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&max->port, co, baud, parity, bits, flow);
}

static struct tty_driver *serial_m3110_con_device(struct console *co,
							int *index)
{
	struct uart_driver *p = co->data;
	*index = co->index;
	return p->tty_driver;
}

static struct uart_driver serial_m3110_reg;
static struct console serial_m3110_console = {
	.name		= "ttyS",
	.write		= serial_m3110_con_write,
	.device		= serial_m3110_con_device,
	.setup		= serial_m3110_con_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &serial_m3110_reg,
};

#define MRST_CONSOLE	(&serial_m3110_console)

static unsigned int serial_m3110_tx_empty(struct uart_port *port)
{
	return 1;
}

static void serial_m3110_stop_tx(struct uart_port *port)
{
	return;
}

/* stop_rx will be called in spin_lock env */
static void serial_m3110_stop_rx(struct uart_port *port)
{
	return;
}

#define WORDS_PER_XFER	128
static inline void send_circ_buf(struct uart_max3110 *max,
				struct circ_buf *xmit)
{
	int len, left = 0;
	u16 obuf[WORDS_PER_XFER], ibuf[WORDS_PER_XFER];
	u8 valid_str[WORDS_PER_XFER];
	int i, j;

	while (!uart_circ_empty(xmit)) {
		left = uart_circ_chars_pending(xmit);
		while (left) {
			len = (left >= WORDS_PER_XFER) ? WORDS_PER_XFER : left;

			memset(obuf, 0, len * 2);
			memset(ibuf, 0, len * 2);
			for (i = 0; i < len; i++) {
				obuf[i] = (u8)xmit->buf[xmit->tail] | WD_TAG;
				xmit->tail = (xmit->tail + 1) &
						(UART_XMIT_SIZE - 1);
			}
			max3110_write_then_read(max, (u8 *)obuf,
						(u8 *)ibuf, len * 2, 0);

			for (i = 0, j = 0; i < len; i++) {
				if (ibuf[i] & MAX3110_READ_DATA_AVAILABLE)
					valid_str[j++] = (u8)(ibuf[i] & 0xff);
			}

			if (j)
				receive_chars(max, valid_str, j);

			max->port.icount.tx += len;
			left -= len;
		}
	}
}

static void transmit_char(struct uart_max3110 *max)
{
	struct uart_port *port = &max->port;
	struct circ_buf *xmit = &port->state->xmit;

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return;

	send_circ_buf(max, xmit);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		serial_m3110_stop_tx(port);
}

/* This will be called by uart_write() and tty_write, can't
 * go to sleep */
static void serial_m3110_start_tx(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);

	if (!test_and_set_bit(UART_TX_NEEDED, &max->uart_flags))
		wake_up_process(max->main_thread);
}

static void receive_chars(struct uart_max3110 *max, unsigned char *str, int len)
{
	struct uart_port *port = &max->port;
	struct tty_struct *tty;
	int usable;

	/* If uart is not opened, just return */
	if (!port->state)
		return;

	tty = port->state->port.tty;
	if (!tty)
		return;	/* receive some char before the tty is opened */

	while (len) {
		usable = tty_buffer_request_room(tty, len);
		if (usable) {
			tty_insert_flip_string(tty, str, usable);
			str += usable;
			port->icount.rx += usable;
			tty_flip_buffer_push(tty);
		}
		len -= usable;
	}
}

static inline void receive_char(struct uart_max3110 *max, u8 ch)
{
	receive_chars(max, &ch, 1);
}

static void max3110_console_receive(struct uart_max3110 *max)
{
	int loop = 1, num, total = 0;
	u8 recv_buf[512], *pbuf;

	pbuf = recv_buf;
	do {
		num = max3110_read_multi(max, 8, pbuf);

		if (num) {
			loop = 10;
			pbuf += num;
			total += num;

			if (total >= 500) {
				receive_chars(max, recv_buf, total);
				pbuf = recv_buf;
				total = 0;
			}
		}
	} while (--loop);

	if (total)
		receive_chars(max, recv_buf, total);
}

static int max3110_main_thread(void *_max)
{
	struct uart_max3110 *max = _max;
	wait_queue_head_t *wq = &max->wq;
	int ret = 0;
	struct circ_buf *xmit = &max->con_xmit;

	init_waitqueue_head(wq);
	pr_info(PR_FMT "start main thread\n");

	do {
		wait_event_interruptible(*wq, max->uart_flags || kthread_should_stop());

		mutex_lock(&max->thread_mutex);

		if (test_and_clear_bit(BIT_IRQ_PENDING, &max->uart_flags))
			max3110_console_receive(max);

		/* first handle console output */
		if (test_and_clear_bit(CON_TX_NEEDED, &max->uart_flags))
			send_circ_buf(max, xmit);

		/* handle uart output */
		if (test_and_clear_bit(UART_TX_NEEDED, &max->uart_flags))
			transmit_char(max);

		mutex_unlock(&max->thread_mutex);

	} while (!kthread_should_stop());

	return ret;
}

#ifdef CONFIG_MRST_MAX3110_IRQ
static irqreturn_t serial_m3110_irq(int irq, void *dev_id)
{
	struct uart_max3110 *max = dev_id;

	/* max3110's irq is a falling edge, not level triggered,
	 * so no need to disable the irq */
	if (!test_and_set_bit(BIT_IRQ_PENDING, &max->uart_flags))
		wake_up_process(max->main_thread);

	return IRQ_HANDLED;
}
#else
/* if don't use RX IRQ, then need a thread to polling read */
static int max3110_read_thread(void *_max)
{
	struct uart_max3110 *max = _max;

	pr_info(PR_FMT "start read thread\n");
	do {
		mutex_lock(&max->thread_mutex);
		max3110_console_receive(max);
		mutex_unlock(&max->thread_mutex);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 20);
	} while (!kthread_should_stop());

	return 0;
}
#endif

static int serial_m3110_startup(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	u16 config = 0;
	int ret = 0;

	if (port->line != 0)
		pr_err(PR_FMT "uart port startup failed\n");

	/* firstly disable all IRQ and config it to 115200, 8n1 */
	config = WC_TAG | WC_FIFO_ENABLE
			| WC_1_STOPBITS
			| WC_8BIT_WORD
			| WC_BAUD_DR2;
	ret = max3110_out(max, config);

	/* as we use thread to handle tx/rx, need set low latency */
	port->state->port.tty->low_latency = 1;

#ifdef CONFIG_MRST_MAX3110_IRQ
	ret = request_irq(max->irq, serial_m3110_irq,
				IRQ_TYPE_EDGE_FALLING, "max3110", max);
	if (ret)
		return ret;

	/* enable RX IRQ only */
	config |= WC_RXA_IRQ_ENABLE;
	max3110_out(max, config);
#else
	/* if IRQ is disabled, start a read thread for input data */
	max->read_thread =
		kthread_run(max3110_read_thread, max, "max3110_read");
#endif

	max->cur_conf = config;
	return 0;
}

static void serial_m3110_shutdown(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	u16 config;

	if (max->read_thread) {
		kthread_stop(max->read_thread);
		max->read_thread = NULL;
	}

#ifdef CONFIG_MRST_MAX3110_IRQ
	free_irq(max->irq, max);
#endif

	/* Disable interrupts from this port */
	config = WC_TAG | WC_SW_SHDI;
	max3110_out(max, config);
}

static void serial_m3110_release_port(struct uart_port *port)
{
}

static int serial_m3110_request_port(struct uart_port *port)
{
	return 0;
}

static void serial_m3110_config_port(struct uart_port *port, int flags)
{
	/* give it fake type */
	port->type = PORT_PXA;
}

static int
serial_m3110_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}


static const char *serial_m3110_type(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	return max->name;
}

static void
serial_m3110_set_termios(struct uart_port *port, struct ktermios *termios,
		       struct ktermios *old)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	unsigned char cval;
	unsigned int baud, parity = 0;
	int clk_div = -1;
	u16 new_conf = max->cur_conf;

	switch (termios->c_cflag & CSIZE) {
	case CS7:
		cval = UART_LCR_WLEN7;
		new_conf |= WC_7BIT_WORD;
		break;
	default:
	case CS8:
		cval = UART_LCR_WLEN8;
		new_conf |= WC_8BIT_WORD;
		break;
	}

	baud = uart_get_baud_rate(port, termios, old, 0, 230400);

	/* first calc the div for 1.8MHZ clock case */
	switch (baud) {
	case 300:
		clk_div = WC_BAUD_DR384;
		break;
	case 600:
		clk_div = WC_BAUD_DR192;
		break;
	case 1200:
		clk_div = WC_BAUD_DR96;
		break;
	case 2400:
		clk_div = WC_BAUD_DR48;
		break;
	case 4800:
		clk_div = WC_BAUD_DR24;
		break;
	case 9600:
		clk_div = WC_BAUD_DR12;
		break;
	case 19200:
		clk_div = WC_BAUD_DR6;
		break;
	case 38400:
		clk_div = WC_BAUD_DR3;
		break;
	case 57600:
		clk_div = WC_BAUD_DR2;
		break;
	case 115200:
		clk_div = WC_BAUD_DR1;
		break;
	case 230400:
		if (max->clock & MAX3110_HIGH_CLK)
			break;
	default:
		/* pick the previous baud rate */
		baud = max->baud;
		clk_div = max->cur_conf & WC_BAUD_DIV_MASK;
		tty_termios_encode_baud_rate(termios, baud, baud);
	}

	if (max->clock & MAX3110_HIGH_CLK) {
		clk_div += 1;
		/* high clk version max3110 doesn't support B300 */
		if (baud == 300)
			baud = 600;
		if (baud == 230400)
			clk_div = WC_BAUD_DR1;
		tty_termios_encode_baud_rate(termios, baud, baud);
	}

	new_conf = (new_conf & ~WC_BAUD_DIV_MASK) | clk_div;
	if (termios->c_cflag & CSTOPB)
		new_conf |= WC_2_STOPBITS;
	else
		new_conf &= ~WC_2_STOPBITS;

	if (termios->c_cflag & PARENB) {
		new_conf |= WC_PARITY_ENABLE;
		parity |= UART_LCR_PARITY;
	} else
		new_conf &= ~WC_PARITY_ENABLE;

	if (!(termios->c_cflag & PARODD))
		parity |= UART_LCR_EPAR;
	max->parity = parity;

	uart_update_timeout(port, termios->c_cflag, baud);

	new_conf |= WC_TAG;
	if (new_conf != max->cur_conf) {
		max3110_out(max, new_conf);
		max->cur_conf = new_conf;
		max->baud = baud;
	}
}

/* don't handle hw handshaking */
static unsigned int serial_m3110_get_mctrl(struct uart_port *port)
{
	return TIOCM_DSR | TIOCM_CAR | TIOCM_DSR;
}

static void serial_m3110_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void serial_m3110_break_ctl(struct uart_port *port, int break_state)
{
}

static void serial_m3110_pm(struct uart_port *port, unsigned int state,
			unsigned int oldstate)
{
}

static void serial_m3110_enable_ms(struct uart_port *port)
{
}

struct uart_ops serial_m3110_ops = {
	.tx_empty	= serial_m3110_tx_empty,
	.set_mctrl	= serial_m3110_set_mctrl,
	.get_mctrl	= serial_m3110_get_mctrl,
	.stop_tx	= serial_m3110_stop_tx,
	.start_tx	= serial_m3110_start_tx,
	.stop_rx	= serial_m3110_stop_rx,
	.enable_ms	= serial_m3110_enable_ms,
	.break_ctl	= serial_m3110_break_ctl,
	.startup	= serial_m3110_startup,
	.shutdown	= serial_m3110_shutdown,
	.set_termios	= serial_m3110_set_termios,	/* must have */
	.pm		= serial_m3110_pm,
	.type		= serial_m3110_type,
	.release_port	= serial_m3110_release_port,
	.request_port	= serial_m3110_request_port,
	.config_port	= serial_m3110_config_port,
	.verify_port	= serial_m3110_verify_port,
};

static struct uart_driver serial_m3110_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "MRST serial",
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= 1,
	.cons		= MRST_CONSOLE,
};

static int serial_m3110_suspend(struct spi_device *spi, pm_message_t state)
{
	return 0;
}

static int serial_m3110_resume(struct spi_device *spi)
{
	return 0;
}

static struct dw_spi_chip spi0_uart = {
	.poll_mode = 1,
	.enable_dma = 0,
	.type = SPI_FRF_SPI,
};

static int serial_m3110_probe(struct spi_device *spi)
{
	struct uart_max3110 *max;
	int ret;
	unsigned char *buffer;
	u16 res;
	max = kzalloc(sizeof(*max), GFP_KERNEL);
	if (!max)
		return -ENOMEM;

	/* set spi info */
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 16;
	max->clock = MAX3110_HIGH_CLK;
	spi->controller_data = &spi0_uart;

	spi_setup(spi);

	max->port.type = PORT_PXA;	/* need apply for a max3110 type */
	max->port.fifosize = 2;		/* only have 16b buffer */
	max->port.ops = &serial_m3110_ops;
	max->port.line = 0;
	max->port.dev = &spi->dev;
	max->port.uartclk = 115200;

	max->spi = spi;
	max->name = spi->modalias;	/* use spi name as the name */
	max->irq = (u16)spi->irq;

	mutex_init(&max->thread_mutex);

	max->word_7bits = 0;
	max->parity = 0;
	max->baud = 0;

	max->cur_conf = 0;
	max->uart_flags = 0;

	/* Check if reading configuration register returns something sane */

	res = RC_TAG;
	ret = max3110_write_then_read(max, (u8 *)&res, (u8 *)&res, 2, 0);
	if (ret < 0 || res == 0 || res == 0xffff) {
		printk(KERN_ERR "MAX3111 deemed not present (conf reg %04x)",
									res);
		ret = -ENODEV;
		goto err_get_page;
	}
	buffer = (unsigned char *)__get_free_page(GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto err_get_page;
	}
	max->con_xmit.buf = (unsigned char *)buffer;
	max->con_xmit.head = max->con_xmit.tail = 0;

	max->main_thread = kthread_run(max3110_main_thread,
					max, "max3110_main");
	if (IS_ERR(max->main_thread)) {
		ret = PTR_ERR(max->main_thread);
		goto err_kthread;
	}

	pmax = max;
	/* give membase a psudo value to pass serial_core's check */
	max->port.membase = (void *)0xff110000;
	uart_add_one_port(&serial_m3110_reg, &max->port);

	return 0;

err_kthread:
	free_page((unsigned long)buffer);
err_get_page:
	pmax = NULL;
	kfree(max);
	return ret;
}

static int max3110_remove(struct spi_device *dev)
{
	struct uart_max3110 *max = pmax;

	if (!pmax)
		return 0;

	pmax = NULL;
	uart_remove_one_port(&serial_m3110_reg, &max->port);

	free_page((unsigned long)max->con_xmit.buf);

	if (max->main_thread)
		kthread_stop(max->main_thread);

	kfree(max);
	return 0;
}

static struct spi_driver uart_max3110_driver = {
	.driver = {
			.name	= "spi_max3111",
			.bus	= &spi_bus_type,
			.owner	= THIS_MODULE,
	},
	.probe		= serial_m3110_probe,
	.remove		= __devexit_p(max3110_remove),
	.suspend	= serial_m3110_suspend,
	.resume		= serial_m3110_resume,
};


int __init serial_m3110_init(void)
{
	int ret = 0;

	ret = uart_register_driver(&serial_m3110_reg);
	if (ret)
		return ret;

	ret = spi_register_driver(&uart_max3110_driver);
	if (ret)
		uart_unregister_driver(&serial_m3110_reg);

	return ret;
}

void __exit serial_m3110_exit(void)
{
	spi_unregister_driver(&uart_max3110_driver);
	uart_unregister_driver(&serial_m3110_reg);
}

module_init(serial_m3110_init);
module_exit(serial_m3110_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("max3110-uart");
