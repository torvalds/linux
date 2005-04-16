/* Serialport functions for debugging
 *
 * Copyright (c) 2000 Axis Communications AB
 *
 * Authors:  Bjorn Wesen
 *
 * Exports:
 *    console_print_etrax(char *buf)
 *    int getDebugChar()
 *    putDebugChar(int)
 *    enableDebugIRQ()
 *    init_etrax_debug()
 *
 * $Log: debugport.c,v $
 * Revision 1.19  2004/10/21 07:26:16  starvik
 * Made it possible to specify console settings on kernel command line.
 *
 * Revision 1.18  2004/10/19 13:07:37  starvik
 * Merge of Linux 2.6.9
 *
 * Revision 1.17  2004/09/29 10:33:46  starvik
 * Resolved a dealock when printing debug from kernel.
 *
 * Revision 1.16  2004/08/24 06:12:19  starvik
 * Whitespace cleanup
 *
 * Revision 1.15  2004/08/16 12:37:19  starvik
 * Merge of Linux 2.6.8
 *
 * Revision 1.14  2004/05/17 13:11:29  starvik
 * Disable DMA until real serial driver is up
 *
 * Revision 1.13  2004/05/14 07:58:01  starvik
 * Merge of changes from 2.4
 *
 * Revision 1.12  2003/09/11 07:29:49  starvik
 * Merge of Linux 2.6.0-test5
 *
 * Revision 1.11  2003/07/07 09:53:36  starvik
 * Revert all the 2.5.74 merge changes to make the console work again
 *
 * Revision 1.9  2003/02/17 17:07:23  starvik
 * Solved the problem with corrupted debug output (from Linux 2.4)
 *   * Wait until DMA, FIFO and pipe is empty before and after transmissions
 *   * Buffer data until a FIFO flush can be triggered.
 *
 * Revision 1.8  2003/01/22 06:48:36  starvik
 * Fixed warnings issued by GCC 3.2.1
 *
 * Revision 1.7  2002/12/12 08:26:32  starvik
 * Don't use C-comments inside CVS comments
 *
 * Revision 1.6  2002/12/11 15:42:02  starvik
 * Extracted v10 (ETRAX 100LX) specific stuff from arch/cris/kernel/
 *
 * Revision 1.5  2002/11/20 06:58:03  starvik
 * Compiles with kgdb
 *
 * Revision 1.4  2002/11/19 14:35:24  starvik
 * Changes from linux 2.4
 * Changed struct initializer syntax to the currently prefered notation
 *
 * Revision 1.3  2002/11/06 09:47:03  starvik
 * Modified for new interrupt macros
 *
 * Revision 1.2  2002/01/21 15:21:50  bjornw
 * Update for kdev_t changes
 *
 * Revision 1.6  2001/04/17 13:58:39  orjanf
 * * Renamed CONFIG_KGDB to CONFIG_ETRAX_KGDB.
 *
 * Revision 1.5  2001/03/26 14:22:05  bjornw
 * Namechange of some config options
 *
 * Revision 1.4  2000/10/06 12:37:26  bjornw
 * Use physical addresses when talking to DMA
 *
 *
 */

#include <linux/config.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <asm/system.h>
#include <asm/arch/svinto.h>
#include <asm/io.h>             /* Get SIMCOUT. */

struct dbg_port
{
  unsigned int index;
  const volatile unsigned* read;
  volatile char* write;
  volatile unsigned* xoff;
  volatile char* baud;
  volatile char* tr_ctrl;
  volatile char* rec_ctrl;
  unsigned long irq;
  unsigned int started;
  unsigned long baudrate;
  unsigned char parity;
  unsigned int bits;
};

struct dbg_port ports[]=
{
  {
    0,
    R_SERIAL0_READ,
    R_SERIAL0_TR_DATA,
    R_SERIAL0_XOFF,
    R_SERIAL0_BAUD,
    R_SERIAL0_TR_CTRL,
    R_SERIAL0_REC_CTRL,
    IO_STATE(R_IRQ_MASK1_SET, ser0_data, set)
  },
  {
    1,
    R_SERIAL1_READ,
    R_SERIAL1_TR_DATA,
    R_SERIAL1_XOFF,
    R_SERIAL1_BAUD,
    R_SERIAL1_TR_CTRL,
    R_SERIAL1_REC_CTRL,
    IO_STATE(R_IRQ_MASK1_SET, ser1_data, set)
  },
  {
    2,
    R_SERIAL2_READ,
    R_SERIAL2_TR_DATA,
    R_SERIAL2_XOFF,
    R_SERIAL2_BAUD,
    R_SERIAL2_TR_CTRL,
    R_SERIAL2_REC_CTRL,
    IO_STATE(R_IRQ_MASK1_SET, ser2_data, set)
  },
  {
    3,
    R_SERIAL3_READ,
    R_SERIAL3_TR_DATA,
    R_SERIAL3_XOFF,
    R_SERIAL3_BAUD,
    R_SERIAL3_TR_CTRL,
    R_SERIAL3_REC_CTRL,
    IO_STATE(R_IRQ_MASK1_SET, ser3_data, set)
  }
};

static struct tty_driver *serial_driver;

struct dbg_port* port =
#if defined(CONFIG_ETRAX_DEBUG_PORT0)
  &ports[0];
#elif defined(CONFIG_ETRAX_DEBUG_PORT1)
  &ports[1];
#elif defined(CONFIG_ETRAX_DEBUG_PORT2)
  &ports[2];
#elif defined(CONFIG_ETRAX_DEBUG_PORT3)
  &ports[3];
#else
  NULL;
#endif
/* Used by serial.c to register a debug_write_function so that the normal
 * serial driver is used for kernel debug output
 */
typedef int (*debugport_write_function)(int i, const char *buf, unsigned int len);

debugport_write_function debug_write_function = NULL;

static void
start_port(void)
{
	unsigned long rec_ctrl = 0;
	unsigned long tr_ctrl = 0;

	if (!port)
		return;

	if (port->started)
		return;
	port->started = 1;

	if (port->index == 0)
	{
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma6);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma6, unused);
	}
	else if (port->index == 1)
	{
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma8);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma8, usb);
	}
	else if (port->index == 2)
	{
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma2);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma2, par0);
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma3);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma3, par0);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, ser2, select);
	}
	else
	{
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma4);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma4, par1);
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma5);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma5, par1);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, ser3, select);
	}

	*R_GEN_CONFIG = genconfig_shadow;

	*port->xoff =
		IO_STATE(R_SERIAL0_XOFF, tx_stop, enable) |
		IO_STATE(R_SERIAL0_XOFF, auto_xoff, disable) |
		IO_FIELD(R_SERIAL0_XOFF, xoff_char, 0);

	switch (port->baudrate)
	{
	case 0:
	case 115200:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c115k2Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c115k2Hz);
		break;
	case 1200:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c1200Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c1200Hz);
		break;
	case 2400:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c2400Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c2400Hz);
		break;
	case 4800:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c4800Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c4800Hz);
		break;
	case 9600:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c9600Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c9600Hz);
		  break;
	case 19200:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c19k2Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c19k2Hz);
		 break;
	case 38400:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c38k4Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c38k4Hz);
		break;
	case 57600:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c57k6Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c57k6Hz);
		break;
	default:
		*port->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c115k2Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c115k2Hz);
		  break;
        }

	if (port->parity == 'E') {
		rec_ctrl =
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par, even) |
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par_en, enable);
		tr_ctrl =
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par, even) |
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par_en, enable);
	} else if (port->parity == 'O') {
		rec_ctrl =
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par, odd) |
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par_en, enable);
		tr_ctrl =
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par, odd) |
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par_en, enable);
	} else {
		rec_ctrl =
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par, even) |
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par_en, disable);
		tr_ctrl =
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par, even) |
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par_en, disable);
	}

	if (port->bits == 7)
	{
		rec_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_bitnr, rec_7bit);
		tr_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_bitnr, tr_7bit);
	}
	else
	{
		rec_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_bitnr, rec_8bit);
		tr_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_bitnr, tr_8bit);
	}

	*port->rec_ctrl =
		IO_STATE(R_SERIAL0_REC_CTRL, dma_err, stop) |
		IO_STATE(R_SERIAL0_REC_CTRL, rec_enable, enable) |
		IO_STATE(R_SERIAL0_REC_CTRL, rts_, active) |
		IO_STATE(R_SERIAL0_REC_CTRL, sampling, middle) |
		IO_STATE(R_SERIAL0_REC_CTRL, rec_stick_par, normal) |
		rec_ctrl;

	*port->tr_ctrl =
		IO_FIELD(R_SERIAL0_TR_CTRL, txd, 0) |
		IO_STATE(R_SERIAL0_TR_CTRL, tr_enable, enable) |
		IO_STATE(R_SERIAL0_TR_CTRL, auto_cts, disabled) |
		IO_STATE(R_SERIAL0_TR_CTRL, stop_bits, one_bit) |
		IO_STATE(R_SERIAL0_TR_CTRL, tr_stick_par, normal) |
		tr_ctrl;
}

static void
console_write_direct(struct console *co, const char *buf, unsigned int len)
{
	int i;
	unsigned long flags;
	local_irq_save(flags);
	/* Send data */
	for (i = 0; i < len; i++) {
		/* Wait until transmitter is ready and send.*/
		while (!(*port->read & IO_MASK(R_SERIAL0_READ, tr_ready)))
			;
		*port->write = buf[i];
	}
	local_irq_restore(flags);
}

static void
console_write(struct console *co, const char *buf, unsigned int len)
{
	if (!port)
		return;

#ifdef CONFIG_SVINTO_SIM
	/* no use to simulate the serial debug output */
	SIMCOUT(buf, len);
	return;
#endif

	start_port();

#ifdef CONFIG_ETRAX_KGDB
	/* kgdb needs to output debug info using the gdb protocol */
	putDebugString(buf, len);
	return;
#endif

	if (debug_write_function)
		debug_write_function(co->index, buf, len);
	else
		console_write_direct(co, buf, len);
}

/* legacy function */

void
console_print_etrax(const char *buf)
{
	console_write(NULL, buf, strlen(buf));
}

/* Use polling to get a single character FROM the debug port */

int
getDebugChar(void)
{
	unsigned long readval;

	do {
		readval = *port->read;
	} while (!(readval & IO_MASK(R_SERIAL0_READ, data_avail)));

	return (readval & IO_MASK(R_SERIAL0_READ, data_in));
}

/* Use polling to put a single character to the debug port */

void
putDebugChar(int val)
{
	while (!(*port->read & IO_MASK(R_SERIAL0_READ, tr_ready)))
		;
	*port->write = val;
}

/* Enable irq for receiving chars on the debug port, used by kgdb */

void
enableDebugIRQ(void)
{
	*R_IRQ_MASK1_SET = port->irq;
	/* use R_VECT_MASK directly, since we really bypass Linux normal
	 * IRQ handling in kgdb anyway, we don't need to use enable_irq
	 */
	*R_VECT_MASK_SET = IO_STATE(R_VECT_MASK_SET, serial, set);

	*port->rec_ctrl = IO_STATE(R_SERIAL0_REC_CTRL, rec_enable, enable);
}

static struct tty_driver*
etrax_console_device(struct console* co, int *index)
{
	return serial_driver;
}

static int __init
console_setup(struct console *co, char *options)
{
	char* s;

	if (options) {
		port = &ports[co->index];
		port->baudrate = 115200;
                port->parity = 'N';
                port->bits = 8;
		port->baudrate = simple_strtoul(options, NULL, 10);
		s = options;
		while(*s >= '0' && *s <= '9')
			s++;
		if (*s) port->parity = *s++;
		if (*s) port->bits   = *s++ - '0';
		port->started = 0;
		start_port();
	}
	return 0;
}

static struct console sercons = {
	name : "ttyS",
	write: console_write,
	read : NULL,
	device : etrax_console_device,
	unblank : NULL,
	setup : console_setup,
	flags : CON_PRINTBUFFER,
	index : -1,
	cflag : 0,
	next : NULL
};
static struct console sercons0 = {
	name : "ttyS",
	write: console_write,
	read : NULL,
	device : etrax_console_device,
	unblank : NULL,
	setup : console_setup,
	flags : CON_PRINTBUFFER,
	index : 0,
	cflag : 0,
	next : NULL
};

static struct console sercons1 = {
	name : "ttyS",
	write: console_write,
	read : NULL,
	device : etrax_console_device,
	unblank : NULL,
	setup : console_setup,
	flags : CON_PRINTBUFFER,
	index : 1,
	cflag : 0,
	next : NULL
};
static struct console sercons2 = {
	name : "ttyS",
	write: console_write,
	read : NULL,
	device : etrax_console_device,
	unblank : NULL,
	setup : console_setup,
	flags : CON_PRINTBUFFER,
	index : 2,
	cflag : 0,
	next : NULL
};
static struct console sercons3 = {
	name : "ttyS",
	write: console_write,
	read : NULL,
	device : etrax_console_device,
	unblank : NULL,
	setup : console_setup,
	flags : CON_PRINTBUFFER,
	index : 3,
	cflag : 0,
	next : NULL
};
/*
 *      Register console (for printk's etc)
 */

int __init
init_etrax_debug(void)
{
	static int first = 1;

	if (!first) {
		if (!port) {
			register_console(&sercons0);
			register_console(&sercons1);
			register_console(&sercons2);
			register_console(&sercons3);
			unregister_console(&sercons);
		}
		return 0;
	}
	first = 0;
	if (port)
		register_console(&sercons);
	return 0;
}

int __init
init_console(void)
{
	serial_driver = alloc_tty_driver(1);
	if (!serial_driver)
		return -ENOMEM;
	return 0;
}

__initcall(init_etrax_debug);
