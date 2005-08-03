/*
 * Copyright (C) 2003, Axis Communications AB.
 */

#include <linux/config.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/arch/hwregs/ser_defs.h>
#include <asm/arch/hwregs/dma_defs.h>
#include <asm/arch/pinmux.h>

#include <asm/irq.h>
#include <asm/arch/hwregs/intr_vect_defs.h>

struct dbg_port
{
	unsigned char nbr;
	unsigned long instance;
	unsigned int started;
	unsigned long baudrate;
	unsigned char parity;
	unsigned int bits;
};

struct dbg_port ports[] =
{
  {
    0,
    regi_ser0,
    0,
    115200,
    'N',
    8
  },
  {
    1,
    regi_ser1,
    0,
    115200,
    'N',
    8
  },
  {
    2,
    regi_ser2,
    0,
    115200,
    'N',
    8
  },
  {
    3,
    regi_ser3,
    0,
    115200,
    'N',
    8
  }
};
static struct dbg_port *port =
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

#ifdef CONFIG_ETRAX_KGDB
static struct dbg_port *kgdb_port =
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
#endif

#ifdef CONFIG_ETRAXFS_SIM
extern void print_str( const char *str );
static char buffer[1024];
static char msg[] = "Debug: ";
static int buffer_pos = sizeof(msg) - 1;
#endif

extern struct tty_driver *serial_driver;

static void
start_port(struct dbg_port* p)
{
	if (!p)
		return;

	if (p->started)
		return;
	p->started = 1;

	if (p->nbr == 1)
		crisv32_pinmux_alloc_fixed(pinmux_ser1);
	else if (p->nbr == 2)
		crisv32_pinmux_alloc_fixed(pinmux_ser2);
	else if (p->nbr == 3)
		crisv32_pinmux_alloc_fixed(pinmux_ser3);

	/* Set up serial port registers */
	reg_ser_rw_tr_ctrl tr_ctrl = {0};
	reg_ser_rw_tr_dma_en tr_dma_en = {0};

	reg_ser_rw_rec_ctrl rec_ctrl = {0};
	reg_ser_rw_tr_baud_div tr_baud_div = {0};
	reg_ser_rw_rec_baud_div rec_baud_div = {0};

	tr_ctrl.base_freq = rec_ctrl.base_freq = regk_ser_f29_493;
	tr_dma_en.en = rec_ctrl.dma_mode = regk_ser_no;
	tr_baud_div.div = rec_baud_div.div = 29493000 / p->baudrate / 8;
	tr_ctrl.en = rec_ctrl.en = 1;

	if (p->parity == 'O')
	{
		tr_ctrl.par_en = regk_ser_yes;
		tr_ctrl.par = regk_ser_odd;
		rec_ctrl.par_en = regk_ser_yes;
		rec_ctrl.par = regk_ser_odd;
	}
	else if (p->parity == 'E')
	{
		tr_ctrl.par_en = regk_ser_yes;
		tr_ctrl.par = regk_ser_even;
		rec_ctrl.par_en = regk_ser_yes;
		rec_ctrl.par = regk_ser_odd;
	}

	if (p->bits == 7)
	{
		tr_ctrl.data_bits = regk_ser_bits7;
		rec_ctrl.data_bits = regk_ser_bits7;
	}

	REG_WR (ser, p->instance, rw_tr_baud_div, tr_baud_div);
	REG_WR (ser, p->instance, rw_rec_baud_div, rec_baud_div);
	REG_WR (ser, p->instance, rw_tr_dma_en, tr_dma_en);
	REG_WR (ser, p->instance, rw_tr_ctrl, tr_ctrl);
	REG_WR (ser, p->instance, rw_rec_ctrl, rec_ctrl);
}

/* No debug */
#ifdef CONFIG_ETRAX_DEBUG_PORT_NULL

static void
console_write(struct console *co, const char *buf, unsigned int len)
{
	return;
}

/* Target debug */
#elif !defined(CONFIG_ETRAXFS_SIM)

static void
console_write_direct(struct console *co, const char *buf, unsigned int len)
{
	int i;
	reg_ser_r_stat_din stat;
	reg_ser_rw_tr_dma_en tr_dma_en, old;

	/* Switch to manual mode */
	tr_dma_en = old = REG_RD (ser, port->instance, rw_tr_dma_en);
	if (tr_dma_en.en == regk_ser_yes) {
		tr_dma_en.en = regk_ser_no;
		REG_WR(ser, port->instance, rw_tr_dma_en, tr_dma_en);
	}

	/* Send data */
	for (i = 0; i < len; i++) {
		/* LF -> CRLF */
		if (buf[i] == '\n') {
			do {
				stat = REG_RD (ser, port->instance, r_stat_din);
			} while (!stat.tr_rdy);
			REG_WR_INT (ser, port->instance, rw_dout, '\r');
		}
		/* Wait until transmitter is ready and send.*/
		do {
			stat = REG_RD (ser, port->instance, r_stat_din);
		} while (!stat.tr_rdy);
		REG_WR_INT (ser, port->instance, rw_dout, buf[i]);
	}

	/* Restore mode */
	if (tr_dma_en.en != old.en)
		REG_WR(ser, port->instance, rw_tr_dma_en, old);
}

static void
console_write(struct console *co, const char *buf, unsigned int len)
{
	if (!port)
		return;
        console_write_direct(co, buf, len);
}



#else

/* VCS debug */

static void
console_write(struct console *co, const char *buf, unsigned int len)
{
	char* pos;
	pos = memchr(buf, '\n', len);
	if (pos) {
		int l = ++pos - buf;
		memcpy(buffer + buffer_pos, buf, l);
		memcpy(buffer, msg, sizeof(msg) - 1);
		buffer[buffer_pos + l] = '\0';
		print_str(buffer);
		buffer_pos = sizeof(msg) - 1;
		if (pos - buf != len) {
			memcpy(buffer + buffer_pos, pos, len - l);
			buffer_pos += len - l;
		}
	} else {
		memcpy(buffer + buffer_pos, buf, len);
		buffer_pos += len;
	}
}

#endif

int raw_printk(const char *fmt, ...)
{
	static char buf[1024];
	int printed_len;
	va_list args;
	va_start(args, fmt);
	printed_len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	console_write(NULL, buf, strlen(buf));
	return printed_len;
}

void
stupid_debug(char* buf)
{
  console_write(NULL, buf, strlen(buf));
}

#ifdef CONFIG_ETRAX_KGDB
/* Use polling to get a single character from the kernel debug port */
int
getDebugChar(void)
{
	reg_ser_rs_status_data stat;
	reg_ser_rw_ack_intr ack_intr = { 0 };

	do {
		stat = REG_RD(ser, kgdb_instance, rs_status_data);
	} while (!stat.data_avail);

	/* Ack the data_avail interrupt. */
	ack_intr.data_avail = 1;
	REG_WR(ser, kgdb_instance, rw_ack_intr, ack_intr);

	return stat.data;
}

/* Use polling to put a single character to the kernel debug port */
void
putDebugChar(int val)
{
	reg_ser_r_status_data stat;
	do {
		stat = REG_RD (ser, kgdb_instance, r_status_data);
	} while (!stat.tr_ready);
	REG_WR (ser, kgdb_instance, rw_data_out, REG_TYPE_CONV(reg_ser_rw_data_out, int, val));
}
#endif /* CONFIG_ETRAX_KGDB */

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
		start_port(port);
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
	dummy_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;

	dummy_driver.open = dummy_open;
	dummy_driver.close = dummy_close;
	dummy_driver.write = dummy_write;
	dummy_driver.write_room = dummy_write_room;
	if (tty_register_driver(&dummy_driver))
		panic("Couldn't register dummy serial driver\n");
}

static struct tty_driver*
crisv32_console_device(struct console* co, int *index)
{
	if (port)
		*index = port->nbr;
        return port ? serial_driver : &dummy_driver;
}

static struct console sercons = {
	name : "ttyS",
	write: console_write,
	read : NULL,
	device : crisv32_console_device,
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
	device : crisv32_console_device,
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
	device : crisv32_console_device,
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
	device : crisv32_console_device,
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
	device : crisv32_console_device,
	unblank : NULL,
	setup : console_setup,
	flags : CON_PRINTBUFFER,
	index : 3,
	cflag : 0,
	next : NULL
};

/* Register console for printk's, etc. */
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
#endif /* CONFIG_ETRAX_KGDB */
	return 0;
}

__initcall(init_etrax_debug);
