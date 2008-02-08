/* Serialport functions for debugging
 *
 * Copyright (c) 2000-2007 Axis Communications AB
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
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <asm/system.h>
#include <asm/arch/svinto.h>
#include <asm/io.h>             /* Get SIMCOUT. */

extern void reset_watchdog(void);

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
    IO_STATE(R_IRQ_MASK1_SET, ser0_data, set),
    0,
    115200,
    'N',
    8
  },
  {
    1,
    R_SERIAL1_READ,
    R_SERIAL1_TR_DATA,
    R_SERIAL1_XOFF,
    R_SERIAL1_BAUD,
    R_SERIAL1_TR_CTRL,
    R_SERIAL1_REC_CTRL,
    IO_STATE(R_IRQ_MASK1_SET, ser1_data, set),
    0,
    115200,
    'N',
    8
  },
  {
    2,
    R_SERIAL2_READ,
    R_SERIAL2_TR_DATA,
    R_SERIAL2_XOFF,
    R_SERIAL2_BAUD,
    R_SERIAL2_TR_CTRL,
    R_SERIAL2_REC_CTRL,
    IO_STATE(R_IRQ_MASK1_SET, ser2_data, set),
    0,
    115200,
    'N',
    8
  },
  {
    3,
    R_SERIAL3_READ,
    R_SERIAL3_TR_DATA,
    R_SERIAL3_XOFF,
    R_SERIAL3_BAUD,
    R_SERIAL3_TR_CTRL,
    R_SERIAL3_REC_CTRL,
    IO_STATE(R_IRQ_MASK1_SET, ser3_data, set),
    0,
    115200,
    'N',
    8
  }
};

#ifdef CONFIG_ETRAX_SERIAL
extern struct tty_driver *serial_driver;
#endif

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

static struct dbg_port* kgdb_port =
#if defined(CONFIG_ETRAX_KGDB_PORT0)
  &ports[0];
#elif defined(CONFIG_ETRAX_KGDB_PORT1)
  &ports[1];
#elif defined(CONFIG_ETRAX_KGDB_PORT2)
  &ports[2];
#elif defined(CONFIG_ETRAX_KGDB_PORT3)
  &ports[3];
#else
  NULL;
#endif

static void
start_port(struct dbg_port* p)
{
	unsigned long rec_ctrl = 0;
	unsigned long tr_ctrl = 0;

	if (!p)
		return;

	if (p->started)
		return;
	p->started = 1;

	if (p->index == 0)
	{
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma6);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma6, unused);
	}
	else if (p->index == 1)
	{
		genconfig_shadow &= ~IO_MASK(R_GEN_CONFIG, dma8);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma8, usb);
	}
	else if (p->index == 2)
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

	*p->xoff =
		IO_STATE(R_SERIAL0_XOFF, tx_stop, enable) |
		IO_STATE(R_SERIAL0_XOFF, auto_xoff, disable) |
		IO_FIELD(R_SERIAL0_XOFF, xoff_char, 0);

	switch (p->baudrate)
	{
	case 0:
	case 115200:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c115k2Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c115k2Hz);
		break;
	case 1200:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c1200Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c1200Hz);
		break;
	case 2400:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c2400Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c2400Hz);
		break;
	case 4800:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c4800Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c4800Hz);
		break;
	case 9600:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c9600Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c9600Hz);
		  break;
	case 19200:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c19k2Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c19k2Hz);
		 break;
	case 38400:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c38k4Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c38k4Hz);
		break;
	case 57600:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c57k6Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c57k6Hz);
		break;
	default:
		*p->baud =
		  IO_STATE(R_SERIAL0_BAUD, tr_baud, c115k2Hz) |
		  IO_STATE(R_SERIAL0_BAUD, rec_baud, c115k2Hz);
		  break;
        }

	if (p->parity == 'E') {
		rec_ctrl =
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par, even) |
		  IO_STATE(R_SERIAL0_REC_CTRL, rec_par_en, enable);
		tr_ctrl =
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par, even) |
		  IO_STATE(R_SERIAL0_TR_CTRL, tr_par_en, enable);
	} else if (p->parity == 'O') {
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
	if (p->bits == 7)
	{
		rec_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_bitnr, rec_7bit);
		tr_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_bitnr, tr_7bit);
	}
	else
	{
		rec_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_bitnr, rec_8bit);
		tr_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_bitnr, tr_8bit);
	}

	*p->rec_ctrl =
		IO_STATE(R_SERIAL0_REC_CTRL, dma_err, stop) |
		IO_STATE(R_SERIAL0_REC_CTRL, rec_enable, enable) |
		IO_STATE(R_SERIAL0_REC_CTRL, rts_, active) |
		IO_STATE(R_SERIAL0_REC_CTRL, sampling, middle) |
		IO_STATE(R_SERIAL0_REC_CTRL, rec_stick_par, normal) |
		rec_ctrl;

	*p->tr_ctrl =
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

        if (!port)
		return;

	local_irq_save(flags);

	/* Send data */
	for (i = 0; i < len; i++) {
		/* LF -> CRLF */
		if (buf[i] == '\n') {
			while (!(*port->read & IO_MASK(R_SERIAL0_READ, tr_ready)))
			;
			*port->write = '\r';
		}
		/* Wait until transmitter is ready and send.*/
		while (!(*port->read & IO_MASK(R_SERIAL0_READ, tr_ready)))
			;
		*port->write = buf[i];
	}

	/*
	 * Feed the watchdog, otherwise it will reset the chip during boot.
	 * The time to send an ordinary boot message line (10-90 chars)
	 * varies between 1-8ms at 115200. What makes up for the additional
	 * 90ms that allows the watchdog to bite?
	*/
	reset_watchdog();

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

	if (!kgdb_port)
		return 0;

	do {
		readval = *kgdb_port->read;
	} while (!(readval & IO_MASK(R_SERIAL0_READ, data_avail)));

	return (readval & IO_MASK(R_SERIAL0_READ, data_in));
}

/* Use polling to put a single character to the debug port */

void
putDebugChar(int val)
{
	if (!kgdb_port)
		return;

	while (!(*kgdb_port->read & IO_MASK(R_SERIAL0_READ, tr_ready)))
		;
	*kgdb_port->write = val;
}

/* Enable irq for receiving chars on the debug port, used by kgdb */

void
enableDebugIRQ(void)
{
	if (!kgdb_port)
		return;

	*R_IRQ_MASK1_SET = kgdb_port->irq;
	/* use R_VECT_MASK directly, since we really bypass Linux normal
	 * IRQ handling in kgdb anyway, we don't need to use enable_irq
	 */
	*R_VECT_MASK_SET = IO_STATE(R_VECT_MASK_SET, serial, set);

	*kgdb_port->rec_ctrl = IO_STATE(R_SERIAL0_REC_CTRL, rec_enable, enable);
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
		start_port(0);
	}
	return 0;
}


/* This is a dummy serial device that throws away anything written to it.
 * This is used when no debug output is wanted.
 */
static struct tty_driver dummy_driver;

static int dummy_open(struct tty_struct *tty, struct file * filp)
{
	return 0;
}

static void dummy_close(struct tty_struct *tty, struct file * filp)
{
}

static int dummy_write(struct tty_struct * tty,
                       const unsigned char *buf, int count)
{
	return count;
}

static int
dummy_write_room(struct tty_struct *tty)
{
	return 8192;
}

void __init
init_dummy_console(void)
{
	memset(&dummy_driver, 0, sizeof(struct tty_driver));
	dummy_driver.driver_name = "serial";
	dummy_driver.name = "ttyS";
	dummy_driver.major = TTY_MAJOR;
	dummy_driver.minor_start = 68;
	dummy_driver.num = 1;       /* etrax100 has 4 serial ports */
	dummy_driver.type = TTY_DRIVER_TYPE_SERIAL;
	dummy_driver.subtype = SERIAL_TYPE_NORMAL;
	dummy_driver.init_termios = tty_std_termios;
	dummy_driver.init_termios.c_cflag =
		B115200 | CS8 | CREAD | HUPCL | CLOCAL; /* is normally B9600 default... */
	dummy_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;

	dummy_driver.open = dummy_open;
	dummy_driver.close = dummy_close;
	dummy_driver.write = dummy_write;
	dummy_driver.write_room = dummy_write_room;
	if (tty_register_driver(&dummy_driver))
		panic("Couldn't register dummy serial driver\n");
}

static struct tty_driver*
etrax_console_device(struct console* co, int *index)
{
	if (port)
		*index = port->index;
	else
		*index = 0;
#ifdef CONFIG_ETRAX_SERIAL
        return port ? serial_driver : &dummy_driver;
#else
	return &dummy_driver;
#endif
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
		unregister_console(&sercons);
		register_console(&sercons0);
		register_console(&sercons1);
		register_console(&sercons2);
		register_console(&sercons3);
                init_dummy_console();
		return 0;
	}

	first = 0;
	register_console(&sercons);
	start_port(port);
#ifdef CONFIG_ETRAX_KGDB
	start_port(kgdb_port);
#endif
	return 0;
}
__initcall(init_etrax_debug);
